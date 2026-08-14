#include "ana_lowerer.h"
#include "ana_encoder.h"
#include "ana_regalloc.h"
#include "elf_emitter.h"
#include "host_interop.h"
#include "../sys/cpu_features.h"
#include "../sys/object_heap.h"
#include "inline_cache.h"
#include "aarch64_backend.h"
#include "armv7_backend.h"
#include "rv64_backend.h"

namespace ana {
namespace backend {

AnaLowerer::AnaLowerer(AnastasiaJitRuntime& runtime) : runtime_(runtime) {}

static bool streq_impl(const char* s1, const char* s2) {
    if (!s1 || !s2) return false;
    if (s1[0] == '.') s1++;
    if (s2[0] == '.') s2++;
    while (*s1 && *s2 && *s1 != ':' && *s2 != ':') {
        if (*s1 != *s2) return false;
        s1++; s2++;
    }
    char c1 = (*s1 == ':') ? '\0' : *s1;
    char c2 = (*s2 == ':') ? '\0' : *s2;
    return c1 == c2;
}

// The /32 opcode family must wrap at 32 bits. Every one of these was lowered
// with a full 64-bit instruction, so `add-int/32 2e9, 2e9` produced
// 4000000000 instead of -294967296. Results are re-normalised by
// sign-extending the low 32 bits back into the 64-bit register.
static bool is_i32_op(frontend::Opcode op) {
    switch (op) {
        case frontend::Opcode::ADD_I32:
        case frontend::Opcode::SUB_I32:
        case frontend::Opcode::MUL_I32:
        case frontend::Opcode::DIV_I32:
        case frontend::Opcode::SHL_I32:
        case frontend::Opcode::SHR_I32:
        case frontend::Opcode::USHR_I32:
            return true;
        default:
            return false;
    }
}

void* AnaLowerer::compile_function(frontend::Function* fn, frontend::Program* prog) {
    if (!fn) return nullptr;

#if defined(__aarch64__) || defined(_M_ARM64)
    AArch64TargetBackend arm_backend(&runtime_);
    return arm_backend.compile_function(fn, prog);
#elif defined(__arm__) || defined(_M_ARM) || defined(__armv7__)
    Armv7TargetBackend armv7_backend(&runtime_);
    return armv7_backend.compile_function(fn, prog);
#elif defined(__riscv) || defined(__riscv__)
    Rv64TargetBackend rv64_backend(&runtime_);
    return rv64_backend.compile_function(fn, prog);
#else

    AnaRegAlloc* regalloc = new AnaRegAlloc();
    regalloc->allocate_registers(fn);

    AnaEncoder* enc = new AnaEncoder();

    struct BlockLabelEntry {
        frontend::BasicBlock* block;
        uint32_t label_id;
    } block_labels[512];
    uint32_t block_count = 0;

    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        if (block_count >= 512) { delete regalloc; delete enc; return nullptr; }
        block_labels[block_count].block = bb;
        block_labels[block_count].label_id = enc->new_label();
        block_count++;
    }

    auto get_block_label = [&](const char* lbl_name, frontend::BasicBlock* target_bb) -> uint32_t {
        if (target_bb) {
            for (uint32_t i = 0; i < block_count; ++i) {
                if (block_labels[i].block == target_bb) return block_labels[i].label_id;
            }
        }
        if (lbl_name) {
            for (uint32_t i = 0; i < block_count; ++i) {
                if (block_labels[i].block && block_labels[i].block->label &&
                    streq_impl(block_labels[i].block->label, lbl_name)) {
                    return block_labels[i].label_id;
                }
            }
        }
        return enc->new_label();
    };

    // Emit Stack Frame Prologue if required.
    // Callee-saved registers are preserved with MOV into reserved frame slots
    // rather than PUSH, so RSP stays 16-byte aligned at every call site.
    if (regalloc->requires_frame()) {
        enc->push_reg(X86Reg::RBP);
        enc->mov_reg_reg(X86Reg::RBP, X86Reg::RSP);
        enc->sub_reg_imm32(X86Reg::RSP, static_cast<int32_t>(regalloc->stack_frame_size()));
        for (uint32_t i = 0; i < regalloc->saved_reg_count(); ++i) {
            enc->mov_mem_reg(X86Reg::RBP, -AnaRegAlloc::saved_reg_disp(i), AnaRegAlloc::saved_reg(i));
        }
    }

    // Save incoming parameter registers (RDI, RSI, RDX...) into stack frame slots
    for (uint32_t p_idx = 0; p_idx < regalloc->param_count(); ++p_idx) {
        X86Reg raw_reg = regalloc->get_param_raw_reg(p_idx);
        RegLocation p_loc = regalloc->get_param_loc(p_idx);
        if (p_loc.kind == RegLocKind::STACK_SPILL) {
            enc->mov_mem_reg(X86Reg::RBP, -p_loc.stack_disp, raw_reg);
        } else if (p_loc.kind == RegLocKind::PHYSICAL_REG) {
            if (p_loc.phys_reg != raw_reg) {
                enc->mov_reg_reg(p_loc.phys_reg, raw_reg);
            }
        }
    }

    auto load_operand = [&](const frontend::Operand& op, X86Reg scratch) -> X86Reg {
        if (op.kind == frontend::OperandKind::CONST_INT) {
            enc->mov_reg_imm64(scratch, op.const_val);
            return scratch;
        } else if (op.kind == frontend::OperandKind::REGISTER) {
            RegLocation loc = regalloc->get_reg_loc(op.reg);
            if (loc.kind == RegLocKind::PHYSICAL_REG) {
                return loc.phys_reg;
            } else {
                enc->mov_reg_mem(scratch, X86Reg::RBP, -loc.stack_disp);
                return scratch;
            }
        }
        return scratch;
    };

    auto store_operand = [&](frontend::Register reg, X86Reg src_reg) {
        RegLocation loc = regalloc->get_reg_loc(reg);
        if (loc.kind == RegLocKind::PHYSICAL_REG) {
            if (loc.phys_reg != src_reg) {
                enc->mov_reg_reg(loc.phys_reg, src_reg);
            }
        } else {
            enc->mov_mem_reg(X86Reg::RBP, -loc.stack_disp, src_reg);
        }
    };

    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        uint32_t current_lbl = get_block_label(bb->label, bb);
        enc->bind_label(current_lbl);

        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            switch (insn->op) {
                case frontend::Opcode::ADD_I32:
                case frontend::Opcode::ADD_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    if (dst_reg == s2 && s2 != X86Reg::R11 && insn->src2.kind != frontend::OperandKind::CONST_INT) {
                        enc->mov_reg_reg(X86Reg::R11, s2);
                        s2 = X86Reg::R11;
                    }
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->add_reg_imm32(dst_reg, static_cast<int32_t>(insn->src2.const_val));
                    } else {
                        enc->add_reg_reg(dst_reg, s2);
                    }
                    if (is_i32_op(insn->op)) enc->movsxd_reg_reg(dst_reg, dst_reg);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::SUB_I32:
                case frontend::Opcode::SUB_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    if (dst_reg == s2 && s2 != X86Reg::R11 && insn->src2.kind != frontend::OperandKind::CONST_INT) {
                        enc->mov_reg_reg(X86Reg::R11, s2);
                        s2 = X86Reg::R11;
                    }
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->sub_reg_imm32(dst_reg, static_cast<int32_t>(insn->src2.const_val));
                    } else {
                        enc->sub_reg_reg(dst_reg, s2);
                    }
                    if (is_i32_op(insn->op)) enc->movsxd_reg_reg(dst_reg, dst_reg);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::NEG_I32:
                case frontend::Opcode::NEG_I64: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    enc->mov_reg_imm64(X86Reg::R11, 0);
                    enc->sub_reg_reg(X86Reg::R11, s1);
                    enc->mov_reg_reg(dst_reg, X86Reg::R11);
                    if (is_i32_op(insn->op)) enc->movsxd_reg_reg(dst_reg, dst_reg);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::CONST_STRING: {
                    const char* str_ptr = runtime_.string_pool().get_or_intern(insn->string_val, insn->string_len, insn->string_hash);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    enc->mov_reg_imm64(dst_reg, reinterpret_cast<uint64_t>(str_ptr));
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::STR_LEN: {
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    enc->mov_reg_imm64(dst_reg, insn->string_len);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::MUL_I32:
                case frontend::Opcode::MUL_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                    if (s2 != X86Reg::R11) enc->mov_reg_reg(X86Reg::R11, s2);
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    enc->mov_reg_reg(dst_reg, s1);
                    enc->imul_reg_reg(dst_reg, X86Reg::R11);
                    if (is_i32_op(insn->op)) enc->movsxd_reg_reg(dst_reg, dst_reg);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::DIV_I32:
                case frontend::Opcode::DIV_I64: {
                    X86Reg dst_phys = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                      ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    bool push_rdx = (dst_phys != X86Reg::RDX);
                    if (push_rdx) enc->push_reg(X86Reg::RDX);

                    X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                    if (s2 != X86Reg::R11) enc->mov_reg_reg(X86Reg::R11, s2);
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    if (s1 != X86Reg::RAX) enc->mov_reg_reg(X86Reg::RAX, s1);
                    // IDIV raises #DE (SIGFPE, process death) for a zero
                    // divisor and for INT_MIN / -1. Neither was guarded.
                    // Divide by zero yields 0; INT_MIN / -1 yields INT_MIN.
                    uint32_t lbl_zero = enc->new_label();
                    uint32_t lbl_neg1 = enc->new_label();
                    uint32_t lbl_done = enc->new_label();

                    enc->test_reg_reg(X86Reg::R11, X86Reg::R11);
                    enc->jz_label(lbl_zero);
                    enc->cmp_reg_imm32(X86Reg::R11, -1);
                    enc->je_label(lbl_neg1);

                    if (insn->op == frontend::Opcode::DIV_I32) {
                        enc->cdq();
                        enc->idiv_reg32(X86Reg::R11);
                        enc->movsxd_reg_reg(X86Reg::RAX, X86Reg::RAX);
                    } else {
                        enc->cqo();
                        enc->idiv_reg(X86Reg::R11);
                    }
                    enc->jmp_label(lbl_done);

                    enc->bind_label(lbl_zero);
                    enc->mov_reg_imm32(X86Reg::RAX, 0);
                    enc->jmp_label(lbl_done);

                    // x / -1 == -x, computed without IDIV so INT_MIN is safe.
                    enc->bind_label(lbl_neg1);
                    enc->imul_reg_imm32(X86Reg::RAX, X86Reg::RAX, -1);
                    if (insn->op == frontend::Opcode::DIV_I32) {
                        enc->movsxd_reg_reg(X86Reg::RAX, X86Reg::RAX);
                    }

                    enc->bind_label(lbl_done);

                    if (dst_phys != X86Reg::RAX) enc->mov_reg_reg(dst_phys, X86Reg::RAX);
                    store_operand(insn->dest.reg, dst_phys);

                    if (push_rdx) enc->pop_reg(X86Reg::RDX);
                    break;
                }
                case frontend::Opcode::AND_I32:
                case frontend::Opcode::AND_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    if (dst_reg == s2 && s2 != X86Reg::R11 && insn->src2.kind != frontend::OperandKind::CONST_INT) {
                        enc->mov_reg_reg(X86Reg::R11, s2);
                        s2 = X86Reg::R11;
                    }
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->and_reg_imm32(dst_reg, static_cast<int32_t>(insn->src2.const_val));
                    } else {
                        enc->and_reg_reg(dst_reg, s2);
                    }
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::OR_I32:
                case frontend::Opcode::OR_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    if (dst_reg == s2 && s2 != X86Reg::R11 && insn->src2.kind != frontend::OperandKind::CONST_INT) {
                        enc->mov_reg_reg(X86Reg::R11, s2);
                        s2 = X86Reg::R11;
                    }
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->or_reg_imm32(dst_reg, static_cast<int32_t>(insn->src2.const_val));
                    } else {
                        enc->or_reg_reg(dst_reg, s2);
                    }
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::XOR_I32:
                case frontend::Opcode::XOR_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    if (dst_reg == s2 && s2 != X86Reg::R11 && insn->src2.kind != frontend::OperandKind::CONST_INT) {
                        enc->mov_reg_reg(X86Reg::R11, s2);
                        s2 = X86Reg::R11;
                    }
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->xor_reg_imm32(dst_reg, static_cast<int32_t>(insn->src2.const_val));
                    } else {
                        enc->xor_reg_reg(dst_reg, s2);
                    }
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::SHL_I32:
                case frontend::Opcode::SHL_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::RCX);
                    if (insn->src2.kind != frontend::OperandKind::CONST_INT && s2 != X86Reg::RCX) {
                        enc->mov_reg_reg(X86Reg::RCX, s2);
                    }
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->shl_reg_imm8(dst_reg, static_cast<uint8_t>(insn->src2.const_val));
                    } else {
                        enc->shl_reg_cl(dst_reg);
                    }
                    if (is_i32_op(insn->op)) enc->movsxd_reg_reg(dst_reg, dst_reg);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::SHR_I32:
                case frontend::Opcode::SHR_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::RCX);
                    if (insn->src2.kind != frontend::OperandKind::CONST_INT && s2 != X86Reg::RCX) {
                        enc->mov_reg_reg(X86Reg::RCX, s2);
                    }
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->sar_reg_imm8(dst_reg, static_cast<uint8_t>(insn->src2.const_val));
                    } else {
                        enc->sar_reg_cl(dst_reg);
                    }
                    if (is_i32_op(insn->op)) enc->movsxd_reg_reg(dst_reg, dst_reg);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::USHR_I32:
                case frontend::Opcode::USHR_I64: {
                    X86Reg s2 = load_operand(insn->src2, X86Reg::RCX);
                    if (insn->src2.kind != frontend::OperandKind::CONST_INT && s2 != X86Reg::RCX) {
                        enc->mov_reg_reg(X86Reg::RCX, s2);
                    }
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->mov_reg_reg(dst_reg, s1);
                    // Logical shift of a sign-extended value produced
                    // 0x7FFFFFFFFFFFFFFF for `ushr-int/32 -1, 1`; zero-extend
                    // to 32 bits first so the shift sees the real i32 value.
                    if (insn->op == frontend::Opcode::USHR_I32) enc->movzxd_reg_reg(dst_reg, dst_reg);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->shr_reg_imm8(dst_reg, static_cast<uint8_t>(insn->src2.const_val));
                    } else {
                        enc->shr_reg_cl(dst_reg);
                    }
                    if (is_i32_op(insn->op)) enc->movsxd_reg_reg(dst_reg, dst_reg);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::BTS_I64: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->bts_reg_imm8(dst_reg, static_cast<uint8_t>(insn->src2.const_val));
                    } else {
                        X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                        enc->bts_reg_reg(dst_reg, s2);
                    }
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::BTR_I64: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->mov_reg_reg(dst_reg, s1);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->btr_reg_imm8(dst_reg, static_cast<uint8_t>(insn->src2.const_val));
                    } else {
                        X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                        enc->btr_reg_reg(dst_reg, s2);
                    }
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::POPCOUNT_I64: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->popcnt_reg_reg(dst_reg, s1);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::LZCNT_I64: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->lzcnt_reg_reg(dst_reg, s1);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::ADD_FLOAT_32: {
                    // f32 must use 32-bit MOVSS loads/stores; MOVSD read and
                    // wrote 8 bytes around a 4-byte ADDSS.
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->movss_xmm_mem(0, base1, 0);
                    enc->movss_xmm_mem(1, base2, 0);
                    enc->addss_xmm_xmm(0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->movss_mem_xmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::ADD_FLOAT_64: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->movsd_xmm_mem(0, base1, 0);
                    enc->movsd_xmm_mem(1, base2, 0);
                    enc->addsd_xmm_xmm(0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->movsd_mem_xmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::SUB_FLOAT_64: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->movsd_xmm_mem(0, base1, 0);
                    enc->movsd_xmm_mem(1, base2, 0);
                    enc->subsd_xmm_xmm(0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->movsd_mem_xmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::MUL_FLOAT_64: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->movsd_xmm_mem(0, base1, 0);
                    enc->movsd_xmm_mem(1, base2, 0);
                    enc->mulsd_xmm_xmm(0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->movsd_mem_xmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::DIV_FLOAT_64: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->movsd_xmm_mem(0, base1, 0);
                    enc->movsd_xmm_mem(1, base2, 0);
                    enc->divsd_xmm_xmm(0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->movsd_mem_xmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::ADD_VECTOR_I32X4: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->movdqu_xmm_mem(0, base1, 0);
                    enc->movdqu_xmm_mem(1, base2, 0);
                    enc->paddd_xmm_xmm(0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->movdqu_mem_xmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::ADD_VECTOR_I32X8: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->vmovdqu_ymm_mem(0, base1, 0);
                    enc->vmovdqu_ymm_mem(1, base2, 0);
                    enc->vpaddd_ymm_ymm(0, 0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->vmovdqu_mem_ymm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::ADD_VECTOR_I32X16: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->vmovdqu_zmm_mem(0, base1, 0);
                    enc->vmovdqu_zmm_mem(1, base2, 0);
                    enc->vpaddd_zmm_zmm(0, 0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->vmovdqu_mem_zmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::MUL_VECTOR_I32X8: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->vmovdqu_ymm_mem(0, base1, 0);
                    enc->vmovdqu_ymm_mem(1, base2, 0);
                    enc->vpmulld_ymm_ymm(0, 0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->vmovdqu_mem_ymm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::SINK_MEM: {
                    X86Reg val_reg = load_operand(insn->src1, X86Reg::RDI);
                    if (val_reg != X86Reg::RDI) {
                        enc->mov_reg_reg(X86Reg::RDI, val_reg);
                    }
                    enc->mov_reg_imm64(X86Reg::RAX, reinterpret_cast<uint64_t>(backend::ana_benchmark_consume));
                    enc->call_reg(X86Reg::RAX);
                    break;
                }
                case frontend::Opcode::SUB_VECTOR_I32X4: {
                    X86Reg base1 = load_operand(insn->src1, X86Reg::RDI);
                    X86Reg base2 = load_operand(insn->src2, X86Reg::RSI);
                    enc->movdqu_xmm_mem(0, base1, 0);
                    enc->movdqu_xmm_mem(1, base2, 0);
                    enc->psubd_xmm_xmm(0, 1);
                    X86Reg dst_base = load_operand(frontend::Operand::make_reg(insn->dest.reg.type, insn->dest.reg.index), X86Reg::RAX);
                    enc->movdqu_mem_xmm(dst_base, 0, 0);
                    break;
                }
                case frontend::Opcode::MOVE_CONST: {
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                    enc->mov_reg_imm64(dst_reg, insn->src1.const_val);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::MOVE: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    store_operand(insn->dest.reg, s1);
                    break;
                }
                case frontend::Opcode::LOAD_MEM: {
                    X86Reg base = load_operand(frontend::Operand::make_reg(insn->src1.mem.base.type, insn->src1.mem.base.index), X86Reg::RAX);
                    X86Reg dst_reg = (regalloc->get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                     ? regalloc->get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RAX;
                    enc->mov_reg_mem(dst_reg, base, insn->src1.mem.offset);
                    store_operand(insn->dest.reg, dst_reg);
                    break;
                }
                case frontend::Opcode::STORE_MEM: {
                    X86Reg base = load_operand(frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index), X86Reg::RAX);
                    X86Reg src = load_operand(insn->src1, X86Reg::R11);
                    enc->mov_mem_reg(base, insn->dest.mem.offset, src);
                    break;
                }
                case frontend::Opcode::BIND_VTABLE: {
                    X86Reg obj = load_operand(insn->src1, X86Reg::RAX);
                    enc->mov_reg_imm64(X86Reg::R11, insn->src2.const_val);
                    enc->mov_mem_reg(obj, 0, X86Reg::R11);
                    break;
                }
                case frontend::Opcode::CALL_VIRT:
                case frontend::Opcode::CALL_VIRT_FAST: {
                    X86Reg obj = load_operand(insn->src1, X86Reg::RDI);
                    if (obj != X86Reg::RDI) enc->mov_reg_reg(X86Reg::RDI, obj);

                    enc->mov_reg_mem(X86Reg::R11, X86Reg::RDI, 0);
                    enc->mov_reg_mem(X86Reg::R11, X86Reg::R11, insn->vtable_slot * 8);
                    enc->call_reg(X86Reg::R11);

                    // Only write back when the instruction actually named a
                    // destination; otherwise this clobbered v0.
                    if (insn->dest.kind == frontend::OperandKind::REGISTER) {
                        store_operand(insn->dest.reg, X86Reg::RAX);
                    }
                    break;
                }
                case frontend::Opcode::NEW_INSTANCE: {
                    // No push/pop around the call: locals now live in
                    // callee-saved registers and RSP must stay 16-byte aligned.
                    uint32_t inst_size = 16;
                    void* vtable_ptr = nullptr;
                    uint32_t class_id = 1;

                    if (prog && insn->target_label) {
                        for (frontend::ClassDecl* c = prog->classes; c != nullptr; c = c->next) {
                            if (c->name && streq_impl(c->name, insn->target_label)) {
                                inst_size = c->size > 0 ? c->size : 16;
                                vtable_ptr = c->vtable_array;
                                break;
                            }
                        }
                    }

                    enc->mov_reg_imm32(X86Reg::RDI, static_cast<int32_t>(inst_size));
                    enc->mov_reg_imm64(X86Reg::RSI, reinterpret_cast<uint64_t>(vtable_ptr));
                    enc->mov_reg_imm32(X86Reg::RDX, static_cast<int32_t>(class_id));
                    enc->mov_reg_imm64(X86Reg::RAX, reinterpret_cast<uint64_t>(&ana::sys::ana_alloc_object));
                    enc->call_reg(X86Reg::RAX);

                    store_operand(insn->dest.reg, X86Reg::RAX);
                    break;
                }
                case frontend::Opcode::GOTO: {
                    if (insn->target_label) {
                        bool is_next = (bb->next && bb->next->label && streq_impl(bb->next->label, insn->target_label));
                        if (!is_next) {
                            uint32_t target_lbl = get_block_label(insn->target_label, nullptr);
                            enc->jmp_label(target_lbl);
                        }
                    }
                    break;
                }
                case frontend::Opcode::IF_EQ:
                case frontend::Opcode::IF_NE:
                case frontend::Opcode::IF_LT:
                case frontend::Opcode::IF_GE: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    if (insn->src2.kind == frontend::OperandKind::CONST_INT) {
                        enc->cmp_reg_imm32(s1, static_cast<int32_t>(insn->src2.const_val));
                    } else {
                        X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                        enc->cmp_reg_reg(s1, s2);
                    }

                    uint32_t target_lbl = get_block_label(insn->target_label, nullptr);
                    if (insn->op == frontend::Opcode::IF_EQ) enc->je_label(target_lbl);
                    else if (insn->op == frontend::Opcode::IF_NE) enc->jne_label(target_lbl);
                    else if (insn->op == frontend::Opcode::IF_LT) enc->jl_label(target_lbl);
                    else if (insn->op == frontend::Opcode::IF_GE) enc->jge_label(target_lbl);
                    break;
                }
                case frontend::Opcode::IF_Z:
                case frontend::Opcode::IF_NZ: {
                    X86Reg s1 = load_operand(insn->src1, X86Reg::RAX);
                    enc->test_reg_reg(s1, s1);
                    uint32_t target_lbl = get_block_label(insn->target_label, nullptr);
                    if (insn->op == frontend::Opcode::IF_Z) enc->jz_label(target_lbl);
                    else if (insn->op == frontend::Opcode::IF_NZ) enc->jnz_label(target_lbl);
                    break;
                }
                case frontend::Opcode::ATOMIC_CAS_I64: {
                    // LOCK CMPXCHG compares against RAX and returns the previous
                    // value in RAX. The comparand was never loaded, so the
                    // exchange compared against whatever RAX happened to hold.
                    // Semantics: dest supplies the expected value and receives
                    // the value that was in memory.
                    X86Reg base = load_operand(frontend::Operand::make_reg(insn->src1.mem.base.type, insn->src1.mem.base.index), X86Reg::RDI);
                    X86Reg desired = load_operand(insn->src2, X86Reg::RSI);
                    if (desired != X86Reg::RSI) {
                        enc->mov_reg_reg(X86Reg::RSI, desired);
                        desired = X86Reg::RSI;
                    }
                    if (insn->dest.kind == frontend::OperandKind::REGISTER) {
                        X86Reg expected = load_operand(insn->dest, X86Reg::RAX);
                        if (expected != X86Reg::RAX) enc->mov_reg_reg(X86Reg::RAX, expected);
                    }
                    enc->lock_cmpxchg_mem_reg(base, insn->src1.mem.offset, desired);
                    if (insn->dest.kind == frontend::OperandKind::REGISTER) {
                        store_operand(insn->dest.reg, X86Reg::RAX);
                    }
                    break;
                }
                case frontend::Opcode::ATOMIC_XCHG_I64: {
                    X86Reg base = load_operand(frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index), X86Reg::RDI);
                    X86Reg src = load_operand(insn->src1, X86Reg::RAX);
                    enc->xchg_mem_reg(base, insn->dest.mem.offset, src);
                    break;
                }
                case frontend::Opcode::ATOMIC_ADD_I64: {
                    X86Reg base = load_operand(frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index), X86Reg::RDI);
                    X86Reg src = load_operand(insn->src1, X86Reg::RAX);
                    enc->lock_add_mem_reg(base, insn->dest.mem.offset, src);
                    break;
                }
                case frontend::Opcode::ATOMIC_AND_I64: {
                    X86Reg base = load_operand(frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index), X86Reg::RDI);
                    X86Reg src = load_operand(insn->src1, X86Reg::RAX);
                    enc->lock_and_mem_reg(base, insn->dest.mem.offset, src);
                    break;
                }
                case frontend::Opcode::ATOMIC_OR_I64: {
                    X86Reg base = load_operand(frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index), X86Reg::RDI);
                    X86Reg src = load_operand(insn->src1, X86Reg::RAX);
                    enc->lock_or_mem_reg(base, insn->dest.mem.offset, src);
                    break;
                }
                case frontend::Opcode::FENCE: {
                    enc->mfence();
                    break;
                }
                case frontend::Opcode::RETURN_VAL: {
                    X86Reg ret_reg = load_operand(insn->src1, X86Reg::RAX);
                    if (ret_reg != X86Reg::RAX) {
                        enc->mov_reg_reg(X86Reg::RAX, ret_reg);
                    }
                    if (regalloc->requires_frame()) {
                        for (uint32_t si = 0; si < regalloc->saved_reg_count(); ++si) {
                            enc->mov_reg_mem(AnaRegAlloc::saved_reg(si), X86Reg::RBP,
                                             -AnaRegAlloc::saved_reg_disp(si));
                        }
                        enc->mov_reg_reg(X86Reg::RSP, X86Reg::RBP);
                        enc->pop_reg(X86Reg::RBP);
                    }
                    enc->ret();
                    break;
                }
                case frontend::Opcode::RETURN_VOID: {
                    if (regalloc->requires_frame()) {
                        for (uint32_t si = 0; si < regalloc->saved_reg_count(); ++si) {
                            enc->mov_reg_mem(AnaRegAlloc::saved_reg(si), X86Reg::RBP,
                                             -AnaRegAlloc::saved_reg_disp(si));
                        }
                        enc->mov_reg_reg(X86Reg::RSP, X86Reg::RBP);
                        enc->pop_reg(X86Reg::RBP);
                    }
                    enc->ret();
                    break;
                }
                default: break;
            }
        }
    }

    if (!enc->resolve_labels()) {
        delete regalloc;
        delete enc;
        return nullptr;
    }

    // The executable page must be sized to the code that was actually
    // generated. Hard-coding 4096 here overflowed the mapping (and crashed)
    // for any function whose body exceeded one page.
    size_t code_sz = enc->code_size();
    if (code_sz == 0 || enc->failed()) {
        delete regalloc;
        delete enc;
        return nullptr;
    }
    size_t map_sz = (code_sz + 4095) & ~static_cast<size_t>(4095);

    void* fn_ptr = ana::sys::raw_mmap(nullptr, map_sz, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!fn_ptr || fn_ptr == reinterpret_cast<void*>(-1)) {
        delete regalloc;
        delete enc;
        return nullptr;
    }

    ana::sys::freestanding_memcpy(fn_ptr, enc->code_bytes(), code_sz);
    delete enc;

    int mprot_res = ana::sys::raw_mprotect(fn_ptr, map_sz, ANA_PROT_READ | ANA_PROT_EXEC);
    ana::sys::clear_icache(fn_ptr, map_sz);
    delete regalloc;

    if (mprot_res != 0) {
        ana::sys::raw_munmap(fn_ptr, map_sz);
        return nullptr;
    }

    // Record the range as JIT-owned so the inline-cache backpatcher is allowed
    // to flip its page protections. Sites outside any registered range are
    // patched without touching protections.
    register_jit_code_region(fn_ptr, map_sz);

    return fn_ptr;
#endif
}

bool AnaLowerer::compile_to_elf(frontend::Program* prog, const char* out_filename) {
    if (!prog || !out_filename) return false;

#if defined(__aarch64__) || defined(_M_ARM64)
    AArch64TargetBackend arm_backend;
    return arm_backend.compile_to_elf(prog, out_filename);
#else

    ElfEmitter* elf = new ElfEmitter();
    SimpleByteBuffer* text_buf = new SimpleByteBuffer();

    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        uint64_t func_offset = text_buf->size();

        AnaRegAlloc regalloc;
        regalloc.allocate_registers(fn);

        AnaEncoder* enc = new AnaEncoder();

        struct BlockLabelEntry {
            frontend::BasicBlock* block;
            uint32_t label_id;
        } block_labels[64];
        uint32_t block_count = 0;

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr && block_count < 64; bb = bb->next) {
            block_labels[block_count].block = bb;
            block_labels[block_count].label_id = enc->new_label();
            block_count++;
        }

        auto get_block_label = [&](const char* lbl_name, frontend::BasicBlock* target_bb) -> uint32_t {
            if (target_bb) {
                for (uint32_t i = 0; i < block_count; ++i) {
                    if (block_labels[i].block == target_bb) return block_labels[i].label_id;
                }
            }
            if (lbl_name) {
                for (uint32_t i = 0; i < block_count; ++i) {
                    if (block_labels[i].block->label && streq_impl(block_labels[i].block->label, lbl_name)) {
                        return block_labels[i].label_id;
                    }
                }
            }
            // Returning 0 here aliased label 0 and produced a branch to the
            // wrong block. Hand back an unbound label so resolve_labels fails.
            return enc->new_label();
        };

        if (regalloc.requires_frame()) {
            enc->push_reg(X86Reg::RBP);
            enc->mov_reg_reg(X86Reg::RBP, X86Reg::RSP);
            enc->sub_reg_imm32(X86Reg::RSP, static_cast<int32_t>(regalloc.stack_frame_size()));
        }

        for (uint32_t p_idx = 0; p_idx < regalloc.param_count(); ++p_idx) {
            X86Reg raw_reg = regalloc.get_param_raw_reg(p_idx);
            RegLocation p_loc = regalloc.get_param_loc(p_idx);
            if (p_loc.kind == RegLocKind::STACK_SPILL) {
                enc->mov_mem_reg(X86Reg::RBP, -p_loc.stack_disp, raw_reg);
            } else if (p_loc.kind == RegLocKind::PHYSICAL_REG) {
                if (p_loc.phys_reg != raw_reg) {
                    enc->mov_reg_reg(p_loc.phys_reg, raw_reg);
                }
            }
        }

        auto load_operand = [&](const frontend::Operand& op, X86Reg scratch) -> X86Reg {
            if (op.kind == frontend::OperandKind::CONST_INT) {
                enc->mov_reg_imm64(scratch, op.const_val);
                return scratch;
            } else if (op.kind == frontend::OperandKind::REGISTER) {
                RegLocation loc = regalloc.get_reg_loc(op.reg);
                if (loc.kind == RegLocKind::PHYSICAL_REG) {
                    return loc.phys_reg;
                } else {
                    enc->mov_reg_mem(scratch, X86Reg::RBP, -loc.stack_disp);
                    return scratch;
                }
            }
            return scratch;
        };

        auto store_operand = [&](frontend::Register reg, X86Reg src_reg) {
            RegLocation loc = regalloc.get_reg_loc(reg);
            if (loc.kind == RegLocKind::PHYSICAL_REG) {
                if (loc.phys_reg != src_reg) {
                    enc->mov_reg_reg(loc.phys_reg, src_reg);
                }
            } else {
                enc->mov_mem_reg(X86Reg::RBP, -loc.stack_disp, src_reg);
            }
        };

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            uint32_t current_lbl = get_block_label(bb->label, bb);
            enc->bind_label(current_lbl);

            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::ADD_I64: {
                        X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                        X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                        X86Reg dst_reg = (regalloc.get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                         ? regalloc.get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                        enc->mov_reg_reg(dst_reg, s1);
                        enc->add_reg_reg(dst_reg, s2);
                        store_operand(insn->dest.reg, dst_reg);
                        break;
                    }
                    case frontend::Opcode::SUB_I64: {
                        X86Reg s1 = load_operand(insn->src1, X86Reg::RCX);
                        X86Reg s2 = load_operand(insn->src2, X86Reg::R11);
                        X86Reg dst_reg = (regalloc.get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                         ? regalloc.get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                        enc->mov_reg_reg(dst_reg, s1);
                        enc->sub_reg_reg(dst_reg, s2);
                        store_operand(insn->dest.reg, dst_reg);
                        break;
                    }
                    case frontend::Opcode::MOVE_CONST: {
                        X86Reg dst_reg = (regalloc.get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                         ? regalloc.get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                        enc->mov_reg_imm64(dst_reg, insn->src1.const_val);
                        store_operand(insn->dest.reg, dst_reg);
                        break;
                    }
                    case frontend::Opcode::CONST_STRING: {
                        uint64_t ro_off = elf->append_rodata(insn->string_val, insn->string_len + 1);
                        X86Reg dst_reg = (regalloc.get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                         ? regalloc.get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                        uint32_t patch_off = static_cast<uint32_t>(enc->code_size() + 3);
                        enc->lea_reg_rip_disp32(dst_reg, 0);
                        store_operand(insn->dest.reg, dst_reg);

                        // Emit R_X86_64_PC32 relocation entry in .rela.text
                        elf->add_relocation(func_offset + patch_off, 2 /* .rodata */, 2 /* R_X86_64_PC32 */, static_cast<int64_t>(ro_off) - 4);
                        break;
                    }
                    case frontend::Opcode::STR_LEN: {
                        X86Reg dst_reg = (regalloc.get_reg_loc(insn->dest.reg).kind == RegLocKind::PHYSICAL_REG)
                                         ? regalloc.get_reg_loc(insn->dest.reg).phys_reg : X86Reg::RCX;
                        enc->mov_reg_imm64(dst_reg, insn->string_len);
                        store_operand(insn->dest.reg, dst_reg);
                        break;
                    }
                    case frontend::Opcode::RETURN_VAL: {
                        X86Reg ret_reg = load_operand(insn->src1, X86Reg::RAX);
                        if (ret_reg != X86Reg::RAX) {
                            enc->mov_reg_reg(X86Reg::RAX, ret_reg);
                        }
                        if (regalloc.requires_frame()) {
                            for (uint32_t si = 0; si < regalloc.saved_reg_count(); ++si) {
                                enc->mov_reg_mem(AnaRegAlloc::saved_reg(si), X86Reg::RBP,
                                                     -AnaRegAlloc::saved_reg_disp(si));
                            }
                            enc->mov_reg_reg(X86Reg::RSP, X86Reg::RBP);
                            enc->pop_reg(X86Reg::RBP);
                        }
                        enc->ret();
                        break;
                    }
                    case frontend::Opcode::RETURN_VOID: {
                        if (regalloc.requires_frame()) {
                            for (uint32_t si = 0; si < regalloc.saved_reg_count(); ++si) {
                                enc->mov_reg_mem(AnaRegAlloc::saved_reg(si), X86Reg::RBP,
                                                     -AnaRegAlloc::saved_reg_disp(si));
                            }
                            enc->mov_reg_reg(X86Reg::RSP, X86Reg::RBP);
                            enc->pop_reg(X86Reg::RBP);
                        }
                        enc->ret();
                        break;
                    }
                    default: break;
                }
            }
        }

        if (!enc->resolve_labels()) {
            delete enc;
            delete text_buf;
            delete elf;
            return false;
        }

        uint64_t func_size = enc->code_size();
        text_buf->write(enc->code_bytes(), func_size);

        elf->add_symbol(fn->name ? fn->name : "anon_func", STB_GLOBAL, STT_FUNC, 1, func_offset, func_size);
        delete enc;
    }

    bool success = elf->write_elf_object(out_filename, text_buf->data(), text_buf->size());
    delete text_buf;
    delete elf;
    return success;
#endif
}

} // namespace backend
} // namespace ana
