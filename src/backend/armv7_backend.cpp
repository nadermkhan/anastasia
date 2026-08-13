#include "armv7_backend.h"
#include "elf_emitter.h"
#include "ana_lowerer.h"
#include "../sys/object_heap.h"
#include "jit_string_pool.h"
#include <cstdlib>

namespace ana {
namespace backend {

static inline uint8_t armv7_u8(Armv7Reg r) {
    return static_cast<uint8_t>(r);
}

Armv7Encoder::Armv7Encoder() : buffer_(nullptr), capacity_(0), size_(0) {
    capacity_ = 256;
    buffer_ = static_cast<uint32_t*>(malloc(capacity_ * sizeof(uint32_t)));
}

Armv7Encoder::~Armv7Encoder() {
    if (buffer_) free(buffer_);
}

void Armv7Encoder::reset() {
    size_ = 0;
}

void Armv7Encoder::emit32(uint32_t code) {
    if (size_ >= capacity_) {
        capacity_ *= 2;
        buffer_ = static_cast<uint32_t*>(realloc(buffer_, capacity_ * sizeof(uint32_t)));
    }
    buffer_[size_++] = code;
}

// ARM Condition 0xE (AL / Always): 0xE...
void Armv7Encoder::add_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // ADD Rd, Rn, Rm: 0xE0800000 | (Rn << 16) | (Rd << 12) | Rm
    uint32_t code = 0xE0800000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | static_cast<uint32_t>(armv7_u8(rm));
    emit32(code);
}

void Armv7Encoder::sub_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // SUB Rd, Rn, Rm: 0xE0400000 | (Rn << 16) | (Rd << 12) | Rm
    uint32_t code = 0xE0400000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | static_cast<uint32_t>(armv7_u8(rm));
    emit32(code);
}

void Armv7Encoder::mul_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // MUL Rd, Rm, Rn: 0xE0000090 | (Rd << 16) | (Rm << 8) | Rn
    uint32_t code = 0xE0000090UL | (static_cast<uint32_t>(armv7_u8(rd)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rm)) << 8)
                                 | static_cast<uint32_t>(armv7_u8(rn));
    emit32(code);
}

void Armv7Encoder::sdiv_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // SDIV Rd, Rn, Rm: 0xE710F010 | (Rd << 16) | (Rm << 8) | Rn
    uint32_t code = 0xE710F010UL | (static_cast<uint32_t>(armv7_u8(rd)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rm)) << 8)
                                 | static_cast<uint32_t>(armv7_u8(rn));
    emit32(code);
}

void Armv7Encoder::and_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // AND Rd, Rn, Rm: 0xE0000000 | (Rn << 16) | (Rd << 12) | Rm
    uint32_t code = 0xE0000000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | static_cast<uint32_t>(armv7_u8(rm));
    emit32(code);
}

void Armv7Encoder::orr_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // ORR Rd, Rn, Rm: 0xE1800000 | (Rn << 16) | (Rd << 12) | Rm
    uint32_t code = 0xE1800000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | static_cast<uint32_t>(armv7_u8(rm));
    emit32(code);
}

void Armv7Encoder::eor_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // EOR Rd, Rn, Rm: 0xE0200000 | (Rn << 16) | (Rd << 12) | Rm
    uint32_t code = 0xE0200000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | static_cast<uint32_t>(armv7_u8(rm));
    emit32(code);
}

void Armv7Encoder::lsl_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // LSL Rd, Rn, Rm: 0xE1A00010 | (Rn << 16) | (Rd << 12) | (Rm << 8)
    uint32_t code = 0xE1A00010UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | (static_cast<uint32_t>(armv7_u8(rm)) << 8);
    emit32(code);
}

void Armv7Encoder::lsr_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm) {
    // LSR Rd, Rn, Rm: 0xE1A00030 | (Rn << 16) | (Rd << 12) | (Rm << 8)
    uint32_t code = 0xE1A00030UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | (static_cast<uint32_t>(armv7_u8(rm)) << 8);
    emit32(code);
}

void Armv7Encoder::mov_reg_imm32(Armv7Reg rd, uint32_t val) {
    uint16_t low16 = static_cast<uint16_t>(val & 0xFFFF);
    uint16_t high16 = static_cast<uint16_t>((val >> 16) & 0xFFFF);
    uint8_t d = armv7_u8(rd);

    // MOVW Rd, #low16: 0xE3000000 | ((low16 & 0xF000) << 4) | (d << 12) | (low16 & 0xFFF)
    uint32_t code_w = 0xE3000000UL | (static_cast<uint32_t>(low16 & 0xF000) << 4)
                                   | (static_cast<uint32_t>(d) << 12)
                                   | static_cast<uint32_t>(low16 & 0xFFF);
    emit32(code_w);

    if (high16 != 0) {
        // MOVT Rd, #high16: 0xE3400000 | ((high16 & 0xF000) << 4) | (d << 12) | (high16 & 0xFFF)
        uint32_t code_t = 0xE3400000UL | (static_cast<uint32_t>(high16 & 0xF000) << 4)
                                       | (static_cast<uint32_t>(d) << 12)
                                       | static_cast<uint32_t>(high16 & 0xFFF);
        emit32(code_t);
    }
}

void Armv7Encoder::mov_reg_reg(Armv7Reg rd, Armv7Reg rm) {
    // MOV Rd, Rm: 0xE1A00000 | (Rd << 12) | Rm
    uint32_t code = 0xE1A00000UL | (static_cast<uint32_t>(armv7_u8(rd)) << 12)
                                 | static_cast<uint32_t>(armv7_u8(rm));
    emit32(code);
}

void Armv7Encoder::str_reg_mem(Armv7Reg rt, Armv7Reg rn, uint32_t offset_bytes) {
    // STR Rt, [Rn, #imm12]: 0xE5800000 | (Rn << 16) | (Rt << 12) | (imm12 & 0xFFF)
    if (offset_bytes <= 4095) {
        uint32_t code = 0xE5800000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                     | (static_cast<uint32_t>(armv7_u8(rt)) << 12)
                                     | (offset_bytes & 0xFFF);
        emit32(code);
    } else {
        mov_reg_imm32(Armv7Reg::IP, offset_bytes);
        add_reg_reg(Armv7Reg::IP, rn, Armv7Reg::IP);
        uint32_t code = 0xE5800000UL | (static_cast<uint32_t>(armv7_u8(Armv7Reg::IP)) << 16)
                                     | (static_cast<uint32_t>(armv7_u8(rt)) << 12);
        emit32(code);
    }
}

void Armv7Encoder::ldr_reg_mem(Armv7Reg rt, Armv7Reg rn, uint32_t offset_bytes) {
    // LDR Rt, [Rn, #imm12]: 0xE5900000 | (Rn << 16) | (Rt << 12) | (imm12 & 0xFFF)
    if (offset_bytes <= 4095) {
        uint32_t code = 0xE5900000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                     | (static_cast<uint32_t>(armv7_u8(rt)) << 12)
                                     | (offset_bytes & 0xFFF);
        emit32(code);
    } else {
        mov_reg_imm32(Armv7Reg::IP, offset_bytes);
        add_reg_reg(Armv7Reg::IP, rn, Armv7Reg::IP);
        uint32_t code = 0xE5900000UL | (static_cast<uint32_t>(armv7_u8(Armv7Reg::IP)) << 16)
                                     | (static_cast<uint32_t>(armv7_u8(rt)) << 12);
        emit32(code);
    }
}

void Armv7Encoder::push_fp_lr() {
    // PUSH {R11, LR}: 0xE92D4800
    emit32(0xE92D4800UL);
}

void Armv7Encoder::pop_fp_pc() {
    // POP {R11, PC}: 0xE8BD8800
    emit32(0xE8BD8800UL);
}

void Armv7Encoder::mov_fp_sp() {
    // MOV R11, SP: 0xE1A0B00D
    emit32(0xE1A0B00DUL);
}

void Armv7Encoder::sub_sp_imm32(uint32_t imm) {
    // SUB SP, SP, #imm: 0xE24DD000 | (imm & 0xFF)
    if (imm <= 255) {
        emit32(0xE24DD000UL | (imm & 0xFF));
    } else {
        mov_reg_imm32(Armv7Reg::IP, imm);
        sub_reg_reg(Armv7Reg::SP, Armv7Reg::SP, Armv7Reg::IP);
    }
}

void Armv7Encoder::add_sp_imm32(uint32_t imm) {
    // ADD SP, SP, #imm: 0xE28DD000 | (imm & 0xFF)
    if (imm <= 255) {
        emit32(0xE28DD000UL | (imm & 0xFF));
    } else {
        mov_reg_imm32(Armv7Reg::IP, imm);
        add_reg_reg(Armv7Reg::SP, Armv7Reg::SP, Armv7Reg::IP);
    }
}

void Armv7Encoder::blr(Armv7Reg rn) {
    // BLX Rn: 0xE12FFF30 | Rn
    emit32(0xE12FFF30UL | static_cast<uint32_t>(armv7_u8(rn)));
}

void Armv7Encoder::cmp_reg_reg(Armv7Reg rn, Armv7Reg rm) {
    // CMP Rn, Rm: 0xE1500000 | (Rn << 16) | Rm
    uint32_t code = 0xE1500000UL | (static_cast<uint32_t>(armv7_u8(rn)) << 16)
                                 | static_cast<uint32_t>(armv7_u8(rm));
    emit32(code);
}

void Armv7Encoder::b_cond(uint8_t cond, int32_t imm24_words) {
    // Bcond imm24: (cond << 28) | 0x0A000000 | ((imm24 - 2) & 0x00FFFFFF)
    uint32_t cond_prefix = (static_cast<uint32_t>(cond & 0xF) << 28) | 0x0A000000UL;
    int32_t rel_words = imm24_words - 2; // ARM PC pipeline offset = +8 bytes = 2 words
    emit32(cond_prefix | (static_cast<uint32_t>(rel_words) & 0x00FFFFFFUL));
}

void Armv7Encoder::b_uncond(int32_t imm24_words) {
    // B imm24: 0xEA000000 | ((imm24 - 2) & 0x00FFFFFF)
    int32_t rel_words = imm24_words - 2;
    emit32(0xEA000000UL | (static_cast<uint32_t>(rel_words) & 0x00FFFFFFUL));
}

void Armv7Encoder::bx_lr() {
    // BX LR: 0xE12FFF1E
    emit32(0xE12FFF1EUL);
}

void Armv7Encoder::nop() {
    // NOP: 0xE1A00000 (MOV R0, R0)
    emit32(0xE1A00000UL);
}

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

static void* alloc_obj_helper(uint32_t size, void* vtable, uint32_t class_id) {
    return sys::ObjectHeap::instance().allocate_object(size, vtable, class_id);
}

Armv7TargetBackend::Armv7TargetBackend() : runtime_(nullptr) {}
Armv7TargetBackend::Armv7TargetBackend(AnastasiaJitRuntime* runtime) : runtime_(runtime) {}
Armv7TargetBackend::~Armv7TargetBackend() {}

void* Armv7TargetBackend::compile_function(frontend::Function* fn, frontend::Program* prog) {
    if (!fn) return nullptr;

    struct LabelOffsetEntry {
        const char* label;
        size_t word_offset;
    };
    LabelOffsetEntry label_offsets[128];
    size_t label_count = 0;

    auto record_label = [&](const char* label, size_t word_off) {
        if (!label) return;
        for (size_t i = 0; i < label_count; ++i) {
            if (streq_impl(label_offsets[i].label, label)) {
                label_offsets[i].word_offset = word_off;
                return;
            }
        }
        if (label_count < 128) {
            label_offsets[label_count++] = { label, word_off };
        }
    };

    auto get_label_offset = [&](const char* label) -> size_t {
        if (!label) return 0;
        for (size_t i = 0; i < label_count; ++i) {
            if (streq_impl(label_offsets[i].label, label)) {
                return label_offsets[i].word_offset;
            }
        }
        return 0;
    };

    auto emit_code = [&](Armv7Encoder& e, bool is_pass2) {
        e.push_fp_lr();
        e.mov_fp_sp();
        e.sub_sp_imm32(256); // 128 bytes for v0..v31 + 32 bytes for p0..p7

        // Save incoming parameters R0..R3 to stack spill slots
        for (uint32_t i = 0; i < 4; ++i) {
            Armv7Reg preg = static_cast<Armv7Reg>(static_cast<uint8_t>(Armv7Reg::R0) + i);
            e.str_reg_mem(preg, Armv7Reg::SP, 128 + i * 4);
        }

        auto load_op = [&](const frontend::Operand& op, Armv7Reg scratch) -> Armv7Reg {
            if (op.kind == frontend::OperandKind::CONST_INT) {
                e.mov_reg_imm32(scratch, static_cast<uint32_t>(op.const_val));
                return scratch;
            } else if (op.kind == frontend::OperandKind::REGISTER) {
                if (op.reg.type == frontend::RegisterType::PARAM) {
                    uint32_t offset = 128 + (op.reg.index & 3) * 4;
                    e.ldr_reg_mem(scratch, Armv7Reg::SP, offset);
                    return scratch;
                } else {
                    uint32_t offset = (op.reg.index & 31) * 4;
                    e.ldr_reg_mem(scratch, Armv7Reg::SP, offset);
                    return scratch;
                }
            }
            return scratch;
        };

        auto store_reg = [&](const frontend::Register& reg, Armv7Reg src_reg) {
            if (reg.type == frontend::RegisterType::PARAM) {
                uint32_t offset = 128 + (reg.index & 3) * 4;
                e.str_reg_mem(src_reg, Armv7Reg::SP, offset);
            } else {
                uint32_t offset = (reg.index & 31) * 4;
                e.str_reg_mem(src_reg, Armv7Reg::SP, offset);
            }
        };

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            if (bb->label) {
                record_label(bb->label, e.code_size() / 4);
            }
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::MOVE_CONST: {
                        e.mov_reg_imm32(Armv7Reg::R8, static_cast<uint32_t>(insn->src1.const_val));
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::MOVE: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        store_reg(insn->dest.reg, r1);
                        break;
                    }
                    case frontend::Opcode::ADD_I64:
                    case frontend::Opcode::ADD_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.add_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::SUB_I64:
                    case frontend::Opcode::SUB_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.sub_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::NEG_I64:
                    case frontend::Opcode::NEG_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        e.mov_reg_imm32(Armv7Reg::R9, 0);
                        e.sub_reg_reg(Armv7Reg::R8, Armv7Reg::R9, r1);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::MUL_I64:
                    case frontend::Opcode::MUL_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.mul_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::DIV_I64:
                    case frontend::Opcode::DIV_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.sdiv_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::AND_I64:
                    case frontend::Opcode::AND_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.and_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::OR_I64:
                    case frontend::Opcode::OR_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.orr_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::XOR_I64:
                    case frontend::Opcode::XOR_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.eor_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::SHL_I64:
                    case frontend::Opcode::SHL_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.lsl_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::SHR_I64:
                    case frontend::Opcode::SHR_I32:
                    case frontend::Opcode::USHR_I64:
                    case frontend::Opcode::USHR_I32: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.lsr_reg_reg(Armv7Reg::R8, r1, r2);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::POPCOUNT_I64: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R0);
                        if (r1 != Armv7Reg::R0) e.mov_reg_reg(Armv7Reg::R0, r1);

                        auto popcnt_fn = [](uint32_t v) -> uint32_t {
                            uint32_t c = 0;
                            while (v) { c += (v & 1); v >>= 1; }
                            return c;
                        };
                        e.mov_reg_imm32(Armv7Reg::IP, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(+popcnt_fn)));
                        e.blr(Armv7Reg::IP);
                        store_reg(insn->dest.reg, Armv7Reg::R0);
                        break;
                    }
                    case frontend::Opcode::LOAD_MEM: {
                        frontend::Operand base_op = frontend::Operand::make_reg(insn->src1.mem.base.type, insn->src1.mem.base.index);
                        Armv7Reg base = load_op(base_op, Armv7Reg::R8);
                        e.ldr_reg_mem(Armv7Reg::R9, base, insn->src1.mem.offset);
                        store_reg(insn->dest.reg, Armv7Reg::R9);
                        break;
                    }
                    case frontend::Opcode::STORE_MEM: {
                        frontend::Operand base_op = frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index);
                        Armv7Reg base = load_op(base_op, Armv7Reg::R8);
                        Armv7Reg val = load_op(insn->src1, Armv7Reg::R9);
                        e.str_reg_mem(val, base, insn->dest.mem.offset);
                        break;
                    }
                    case frontend::Opcode::CALL_VIRT:
                    case frontend::Opcode::CALL_VIRT_FAST: {
                        Armv7Reg obj = load_op(insn->src1, Armv7Reg::R0);
                        if (obj != Armv7Reg::R0) e.mov_reg_reg(Armv7Reg::R0, obj);

                        e.ldr_reg_mem(Armv7Reg::R8, Armv7Reg::R0, 0); // vtable ptr
                        e.ldr_reg_mem(Armv7Reg::R9, Armv7Reg::R8, insn->vtable_slot * 4); // fn ptr
                        e.blr(Armv7Reg::R9);

                        if (insn->dest.kind == frontend::OperandKind::REGISTER) {
                            store_reg(insn->dest.reg, Armv7Reg::R0);
                        }
                        break;
                    }
                    case frontend::Opcode::NEW_INSTANCE: {
                        uint32_t inst_size = 32;
                        void* vtable_ptr = nullptr;
                        uint32_t class_id = 1;

                        if (prog && insn->target_label) {
                            for (frontend::ClassDecl* c = prog->classes; c != nullptr; c = c->next) {
                                if (c->name && streq_impl(c->name, insn->target_label)) {
                                    inst_size = c->size > 0 ? c->size + 16 : 32;
                                    vtable_ptr = c->vtable_array;
                                    break;
                                }
                            }
                        }

                        e.mov_reg_imm32(Armv7Reg::R0, inst_size);
                        e.mov_reg_imm32(Armv7Reg::R1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vtable_ptr)));
                        e.mov_reg_imm32(Armv7Reg::R2, class_id);
                        e.mov_reg_imm32(Armv7Reg::IP, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&alloc_obj_helper)));
                        e.blr(Armv7Reg::IP);

                        store_reg(insn->dest.reg, Armv7Reg::R0);
                        break;
                    }
                    case frontend::Opcode::CONST_STRING: {
                        const char* str_ptr = (runtime_ && insn->string_val) ? runtime_->string_pool().get_or_intern(insn->string_val, insn->string_len, insn->string_hash) : insn->string_val;
                        e.mov_reg_imm32(Armv7Reg::R8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(str_ptr)));
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::IF_GE:
                    case frontend::Opcode::IF_LT:
                    case frontend::Opcode::IF_EQ:
                    case frontend::Opcode::IF_NE: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R8);
                        Armv7Reg r2 = load_op(insn->src2, Armv7Reg::R9);
                        e.cmp_reg_reg(r1, r2);
                        uint8_t cond = 0xA; // GE
                        if (insn->op == frontend::Opcode::IF_LT) cond = 0xB;
                        else if (insn->op == frontend::Opcode::IF_EQ) cond = 0x0;
                        else if (insn->op == frontend::Opcode::IF_NE) cond = 0x1;

                        int32_t delta = 2;
                        if (is_pass2 && insn->target_label) {
                            size_t target_w = get_label_offset(insn->target_label);
                            size_t current_w = (e.code_size() / 4);
                            delta = static_cast<int32_t>(target_w) - static_cast<int32_t>(current_w);
                        }
                        e.b_cond(cond, delta);
                        break;
                    }
                    case frontend::Opcode::GOTO: {
                        int32_t delta = -5;
                        if (is_pass2 && insn->target_label) {
                            size_t target_w = get_label_offset(insn->target_label);
                            size_t current_w = (e.code_size() / 4);
                            delta = static_cast<int32_t>(target_w) - static_cast<int32_t>(current_w);
                        }
                        e.b_uncond(delta);
                        break;
                    }
                    case frontend::Opcode::STR_LEN: {
                        e.mov_reg_imm32(Armv7Reg::R8, insn->string_len);
                        store_reg(insn->dest.reg, Armv7Reg::R8);
                        break;
                    }
                    case frontend::Opcode::RETURN_VAL: {
                        Armv7Reg r1 = load_op(insn->src1, Armv7Reg::R0);
                        if (r1 != Armv7Reg::R0) e.mov_reg_reg(Armv7Reg::R0, r1);
                        e.add_sp_imm32(256);
                        e.pop_fp_pc();
                        break;
                    }
                    case frontend::Opcode::RETURN_VOID: {
                        e.add_sp_imm32(256);
                        e.pop_fp_pc();
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    };

    // Pass 1: Measure basic block label word offsets
    Armv7Encoder temp_enc;
    emit_code(temp_enc, false);

    // Pass 2: Emit final code with exact relative branch offsets
    Armv7Encoder enc;
    emit_code(enc, true);

    size_t sz = enc.code_size();
    if (sz == 0) return nullptr;

    void* exec_mem = CustomVMemProvider::alloc_rw(sz);
    if (!exec_mem) return nullptr;

    sys::freestanding_memcpy(exec_mem, enc.code_bytes(), sz);
    CustomVMemProvider::make_rx(exec_mem, sz);
    sys::clear_icache(exec_mem, sz);

    return exec_mem;
}

bool Armv7TargetBackend::compile_to_elf(frontend::Program* prog, const char* out_filename) {
    if (!prog || !out_filename) return false;

    ElfEmitter* elf = new ElfEmitter();
    elf->set_machine_arch(40); // EM_ARM (32-bit ARM)
    SimpleByteBuffer* text_buf = new SimpleByteBuffer();

    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        uint64_t func_offset = text_buf->size();

        Armv7Encoder enc;
        enc.push_fp_lr();
        enc.mov_fp_sp();

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::ADD_I64:
                    case frontend::Opcode::ADD_I32:
                        enc.add_reg_reg(Armv7Reg::R0, Armv7Reg::R0, Armv7Reg::R1);
                        break;
                    case frontend::Opcode::SUB_I64:
                    case frontend::Opcode::SUB_I32:
                        enc.sub_reg_reg(Armv7Reg::R0, Armv7Reg::R0, Armv7Reg::R1);
                        break;
                    case frontend::Opcode::MUL_I32:
                        enc.mul_reg_reg(Armv7Reg::R0, Armv7Reg::R0, Armv7Reg::R1);
                        break;
                    case frontend::Opcode::RETURN_VAL:
                    case frontend::Opcode::RETURN_VOID:
                        enc.pop_fp_pc();
                        break;
                    default:
                        break;
                }
            }
        }

        uint64_t func_size = enc.code_size();
        text_buf->write(enc.code_bytes(), func_size);

        elf->add_symbol(fn->name ? fn->name : "anon_func", STB_GLOBAL, STT_FUNC, 1, func_offset, func_size);
    }

    bool success = elf->write_elf_object(out_filename, text_buf->data(), text_buf->size());
    delete text_buf;
    delete elf;
    return success;
}

} // namespace backend
} // namespace ana
