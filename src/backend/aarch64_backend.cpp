#include "aarch64_backend.h"
#include "elf_emitter.h"
#include "../sys/object_heap.h"

namespace ana {
namespace backend {

static uint8_t arm64_u8(Arm64Reg r) {
    return static_cast<uint8_t>(r) & 31;
}

AArch64Encoder::AArch64Encoder() : buffer_(nullptr), capacity_(16384), size_(0) {
    size_t alloc_bytes = capacity_ * sizeof(uint32_t);
    void* ptr = sys::raw_mmap(nullptr, alloc_bytes, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (ptr == (void*)-1 || !ptr) {
        buffer_ = nullptr;
        capacity_ = 0;
    } else {
        buffer_ = static_cast<uint32_t*>(ptr);
    }
}

AArch64Encoder::~AArch64Encoder() {
    if (buffer_ && buffer_ != (uint32_t*)-1 && capacity_ > 0) {
        sys::raw_munmap(buffer_, capacity_ * sizeof(uint32_t));
        buffer_ = nullptr;
    }
}

void AArch64Encoder::reset() {
    size_ = 0;
}

void AArch64Encoder::emit32(uint32_t code) {
    if (!buffer_ || size_ >= capacity_) return;
    buffer_[size_++] = code;
}

void AArch64Encoder::add_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // ADD Xd, Xn, Xm: 0x8B000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x8B000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::sub_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // SUB Xd, Xn, Xm: 0xCB000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0xCB000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::mul_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // MUL Xd, Xn, Xm: 0x9B007C00 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9B007C00UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::sdiv_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // SDIV Xd, Xn, Xm: 0x9AC00C00 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9AC00C00UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::and_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // AND Xd, Xn, Xm: 0x8A000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x8A000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::orr_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // ORR Xd, Xn, Xm: 0xAA000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0xAA000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::eor_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // EOR Xd, Xn, Xm: 0xCA000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0xCA000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::lsl_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // LSLV Xd, Xn, Xm: 0x9AC02000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9AC02000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::lsr_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // LSRV Xd, Xn, Xm: 0x9AC02400 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9AC02400UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::mov_reg_imm64(Arm64Reg rd, uint64_t val) {
    uint8_t d = arm64_u8(rd);
    uint16_t c0 = static_cast<uint16_t>(val & 0xFFFF);
    uint16_t c1 = static_cast<uint16_t>((val >> 16) & 0xFFFF);
    uint16_t c2 = static_cast<uint16_t>((val >> 32) & 0xFFFF);
    uint16_t c3 = static_cast<uint16_t>((val >> 48) & 0xFFFF);

    // MOVZ Xd, #c0, LSL #0
    uint32_t code = 0xD2800000UL | (static_cast<uint32_t>(c0) << 5) | static_cast<uint32_t>(d);
    emit32(code);

    if (c1 != 0) {
        // MOVK Xd, #c1, LSL #16
        emit32(0xF2A00000UL | (static_cast<uint32_t>(c1) << 5) | static_cast<uint32_t>(d));
    }
    if (c2 != 0) {
        // MOVK Xd, #c2, LSL #32
        emit32(0xF2C00000UL | (static_cast<uint32_t>(c2) << 5) | static_cast<uint32_t>(d));
    }
    if (c3 != 0) {
        // MOVK Xd, #c3, LSL #48
        emit32(0xF2E00000UL | (static_cast<uint32_t>(c3) << 5) | static_cast<uint32_t>(d));
    }
}

void AArch64Encoder::mov_reg_reg(Arm64Reg rd, Arm64Reg rm) {
    // MOV Xd, Xm -> ORR Xd, XZR, Xm
    orr_reg_reg(rd, Arm64Reg::XZR, rm);
}

void AArch64Encoder::str_reg_mem(Arm64Reg rt, Arm64Reg rn, uint32_t offset_bytes) {
    if (offset_bytes % 8 == 0 && offset_bytes <= 32760) {
        uint32_t imm12 = (offset_bytes / 8) & 0xFFF;
        uint32_t code = 0xF9000000UL | (imm12 << 10)
                                     | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                     | static_cast<uint32_t>(arm64_u8(rt));
        emit32(code);
    } else if (offset_bytes < 256) {
        // STUR Xt, [Xn, #simm9]: 0xF8000000 | ((simm9 & 0x1FF) << 12) | (Rn << 5) | Rt
        uint32_t imm9 = offset_bytes & 0x1FF;
        uint32_t code = 0xF8000000UL | (imm9 << 12)
                                     | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                     | static_cast<uint32_t>(arm64_u8(rt));
        emit32(code);
    } else {
        mov_reg_imm64(Arm64Reg::X16, offset_bytes);
        add_reg_reg(Arm64Reg::X16, rn, Arm64Reg::X16);
        uint32_t code = 0xF9000000UL | (static_cast<uint32_t>(arm64_u8(Arm64Reg::X16)) << 5)
                                     | static_cast<uint32_t>(arm64_u8(rt));
        emit32(code);
    }
}

void AArch64Encoder::ldr_reg_mem(Arm64Reg rt, Arm64Reg rn, uint32_t offset_bytes) {
    if (offset_bytes % 8 == 0 && offset_bytes <= 32760) {
        uint32_t imm12 = (offset_bytes / 8) & 0xFFF;
        uint32_t code = 0xF9400000UL | (imm12 << 10)
                                     | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                     | static_cast<uint32_t>(arm64_u8(rt));
        emit32(code);
    } else if (offset_bytes < 256) {
        // LDUR Xt, [Xn, #simm9]: 0xF8400000 | ((simm9 & 0x1FF) << 12) | (Rn << 5) | Rt
        uint32_t imm9 = offset_bytes & 0x1FF;
        uint32_t code = 0xF8400000UL | (imm9 << 12)
                                     | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                     | static_cast<uint32_t>(arm64_u8(rt));
        emit32(code);
    } else {
        mov_reg_imm64(Arm64Reg::X16, offset_bytes);
        add_reg_reg(Arm64Reg::X16, rn, Arm64Reg::X16);
        uint32_t code = 0xF9400000UL | (static_cast<uint32_t>(arm64_u8(Arm64Reg::X16)) << 5)
                                     | static_cast<uint32_t>(arm64_u8(rt));
        emit32(code);
    }
}

void AArch64Encoder::push_fp_lr() {
    // STP X29, X30, [SP, #-16]!
    emit32(0xA9BF7BFDUL);
}

void AArch64Encoder::pop_fp_lr() {
    // LDP X29, X30, [SP], #16
    emit32(0xA8C17BFDUL);
}

void AArch64Encoder::mov_fp_sp() {
    // ADD X29, SP, #0 -> 0x910007FD
    emit32(0x910007FDUL);
}

void AArch64Encoder::sub_sp_imm32(uint32_t imm) {
    // SUB SP, SP, #imm12: 0xD10003FF | ((imm12 & 0xFFF) << 10)
    uint32_t imm12 = imm & 0xFFF;
    emit32(0xD10003FFUL | (imm12 << 10));
}

void AArch64Encoder::add_sp_imm32(uint32_t imm) {
    // ADD SP, SP, #imm12: 0x910003FF | ((imm12 & 0xFFF) << 10)
    uint32_t imm12 = imm & 0xFFF;
    emit32(0x910003FFUL | (imm12 << 10));
}

void AArch64Encoder::adrp(Arm64Reg rd, int32_t imm21_pages) {
    uint32_t imm21 = static_cast<uint32_t>(imm21_pages) & 0x1FFFFF;
    uint32_t immlo = imm21 & 3;
    uint32_t immhi = (imm21 >> 2) & 0x7FFFF;
    uint32_t code = 0x90000000UL | (immlo << 29) | (immhi << 5) | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::add_imm12(Arm64Reg rd, Arm64Reg rn, uint16_t imm12) {
    uint32_t code = 0x91000000UL | ((static_cast<uint32_t>(imm12) & 0xFFF) << 10)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::blr(Arm64Reg rn) {
    uint32_t code = 0xD63F0000UL | (static_cast<uint32_t>(arm64_u8(rn)) << 5);
    emit32(code);
}

void AArch64Encoder::cmp_reg_reg(Arm64Reg rn, Arm64Reg rm) {
    uint32_t code = 0xEB00001FUL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5);
    emit32(code);
}

void AArch64Encoder::b_cond(uint8_t cond, int32_t imm19_words) {
    uint32_t code = 0x54000000UL | ((static_cast<uint32_t>(imm19_words) & 0x7FFFFUL) << 5) | (cond & 0xF);
    emit32(code);
}

void AArch64Encoder::b_uncond(int32_t imm26_words) {
    uint32_t code = 0x14000000UL | (static_cast<uint32_t>(imm26_words) & 0x3FFFFFFUL);
    emit32(code);
}

void AArch64Encoder::ret() {
    // RET (X30)
    emit32(0xD65F03C0UL);
}

void AArch64Encoder::nop() {
    // NOP: 0xD503201F
    emit32(0xD503201FUL);
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

AArch64TargetBackend::AArch64TargetBackend() : runtime_(nullptr) {}
AArch64TargetBackend::AArch64TargetBackend(AnastasiaJitRuntime* runtime) : runtime_(runtime) {}
AArch64TargetBackend::~AArch64TargetBackend() {}

void* AArch64TargetBackend::compile_function(frontend::Function* fn, frontend::Program* prog) {
    (void)prog;
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

    auto emit_code = [&](AArch64Encoder& e, bool is_pass2) {
        e.push_fp_lr();
        e.mov_fp_sp();
        e.sub_sp_imm32(320); // 256 bytes for v0..v31 + 64 bytes for p0..p7

        // Save parameter registers X0..X7 to stack spill slots
        for (uint32_t i = 0; i < 8; ++i) {
            Arm64Reg preg = static_cast<Arm64Reg>(static_cast<uint8_t>(Arm64Reg::X0) + i);
            e.str_reg_mem(preg, Arm64Reg::SP, 256 + i * 8);
        }

        auto load_op = [&](const frontend::Operand& op, Arm64Reg scratch) -> Arm64Reg {
            if (op.kind == frontend::OperandKind::CONST_INT) {
                e.mov_reg_imm64(scratch, op.const_val);
                return scratch;
            } else if (op.kind == frontend::OperandKind::REGISTER) {
                if (op.reg.type == frontend::RegisterType::PARAM) {
                    uint32_t offset = 256 + (op.reg.index & 7) * 8;
                    e.ldr_reg_mem(scratch, Arm64Reg::SP, offset);
                    return scratch;
                } else {
                    uint32_t offset = (op.reg.index & 31) * 8;
                    e.ldr_reg_mem(scratch, Arm64Reg::SP, offset);
                    return scratch;
                }
            }
            return scratch;
        };

        auto store_reg = [&](const frontend::Register& reg, Arm64Reg src_reg) {
            if (reg.type == frontend::RegisterType::PARAM) {
                uint32_t offset = 256 + (reg.index & 7) * 8;
                e.str_reg_mem(src_reg, Arm64Reg::SP, offset);
            } else {
                uint32_t offset = (reg.index & 31) * 8;
                e.str_reg_mem(src_reg, Arm64Reg::SP, offset);
            }
        };

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            if (bb->label) {
                record_label(bb->label, e.code_size() / 4);
            }
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::MOVE_CONST: {
                        e.mov_reg_imm64(Arm64Reg::X9, insn->src1.const_val);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::MOVE: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        store_reg(insn->dest.reg, r1);
                        break;
                    }
                    case frontend::Opcode::ADD_I64:
                    case frontend::Opcode::ADD_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.add_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::SUB_I64:
                    case frontend::Opcode::SUB_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.sub_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::NEG_I64:
                    case frontend::Opcode::NEG_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        e.sub_reg_reg(Arm64Reg::X9, Arm64Reg::XZR, r1);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::MUL_I64:
                    case frontend::Opcode::MUL_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.mul_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::DIV_I64:
                    case frontend::Opcode::DIV_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.sdiv_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::AND_I64:
                    case frontend::Opcode::AND_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.and_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::OR_I64:
                    case frontend::Opcode::OR_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.orr_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::XOR_I64:
                    case frontend::Opcode::XOR_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.eor_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::SHL_I64:
                    case frontend::Opcode::SHL_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.lsl_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::SHR_I64:
                    case frontend::Opcode::SHR_I32:
                    case frontend::Opcode::USHR_I64:
                    case frontend::Opcode::USHR_I32: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.lsr_reg_reg(Arm64Reg::X9, r1, r2);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::LOAD_MEM: {
                        frontend::Operand base_op = frontend::Operand::make_reg(insn->src1.mem.base.type, insn->src1.mem.base.index);
                        Arm64Reg base = load_op(base_op, Arm64Reg::X9);
                        e.ldr_reg_mem(Arm64Reg::X10, base, insn->src1.mem.offset);
                        store_reg(insn->dest.reg, Arm64Reg::X10);
                        break;
                    }
                    case frontend::Opcode::STORE_MEM: {
                        frontend::Operand base_op = frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index);
                        Arm64Reg base = load_op(base_op, Arm64Reg::X9);
                        Arm64Reg val = load_op(insn->src1, Arm64Reg::X10);
                        e.str_reg_mem(val, base, insn->dest.mem.offset);
                        break;
                    }
                    case frontend::Opcode::ATOMIC_ADD_I64: {
                        frontend::Operand base_op = frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index);
                        Arm64Reg base = load_op(base_op, Arm64Reg::X9);
                        Arm64Reg val = load_op(insn->src1, Arm64Reg::X10);
                        e.ldr_reg_mem(Arm64Reg::X11, base, insn->dest.mem.offset);
                        e.add_reg_reg(Arm64Reg::X11, Arm64Reg::X11, val);
                        e.str_reg_mem(Arm64Reg::X11, base, insn->dest.mem.offset);
                        break;
                    }
                    case frontend::Opcode::POPCOUNT_I64: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X0);
                        if (r1 != Arm64Reg::X0) e.mov_reg_reg(Arm64Reg::X0, r1);

                        auto popcnt_fn = [](uint64_t v) -> uint64_t {
                            uint64_t c = 0;
                            while (v) { c += (v & 1); v >>= 1; }
                            return c;
                        };
                        e.mov_reg_imm64(Arm64Reg::X16, reinterpret_cast<uint64_t>(+popcnt_fn));
                        e.blr(Arm64Reg::X16);
                        store_reg(insn->dest.reg, Arm64Reg::X0);
                        break;
                    }
                    case frontend::Opcode::BTS_I64: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.mov_reg_imm64(Arm64Reg::X11, 1ULL);
                        e.lsl_reg_reg(Arm64Reg::X11, Arm64Reg::X11, r2);
                        e.orr_reg_reg(Arm64Reg::X9, r1, Arm64Reg::X11);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::BTR_I64: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.mov_reg_imm64(Arm64Reg::X11, 1ULL);
                        e.lsl_reg_reg(Arm64Reg::X11, Arm64Reg::X11, r2);
                        e.eor_reg_reg(Arm64Reg::X9, r1, Arm64Reg::X11);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::CALL_VIRT:
                    case frontend::Opcode::CALL_VIRT_FAST: {
                        Arm64Reg obj = load_op(insn->src1, Arm64Reg::X0);
                        if (obj != Arm64Reg::X0) e.mov_reg_reg(Arm64Reg::X0, obj);

                        e.ldr_reg_mem(Arm64Reg::X9, Arm64Reg::X0, 0); // vtable ptr
                        e.ldr_reg_mem(Arm64Reg::X10, Arm64Reg::X9, insn->vtable_slot * 8); // fn ptr
                        e.blr(Arm64Reg::X10);

                        if (insn->dest.kind == frontend::OperandKind::REGISTER) {
                            store_reg(insn->dest.reg, Arm64Reg::X0);
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

                        e.mov_reg_imm64(Arm64Reg::X0, static_cast<uint64_t>(inst_size));
                        e.mov_reg_imm64(Arm64Reg::X1, reinterpret_cast<uint64_t>(vtable_ptr));
                        e.mov_reg_imm64(Arm64Reg::X2, static_cast<uint64_t>(class_id));
                        e.mov_reg_imm64(Arm64Reg::X16, reinterpret_cast<uint64_t>(&alloc_obj_helper));
                        e.blr(Arm64Reg::X16);

                        store_reg(insn->dest.reg, Arm64Reg::X0);
                        break;
                    }
                    case frontend::Opcode::CONST_STRING: {
                        const char* str_ptr = (runtime_ && insn->string_val) ? runtime_->string_pool().get_or_intern(insn->string_val, insn->string_len, insn->string_hash) : insn->string_val;
                        e.mov_reg_imm64(Arm64Reg::X9, reinterpret_cast<uint64_t>(str_ptr));
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::IF_GE:
                    case frontend::Opcode::IF_LT:
                    case frontend::Opcode::IF_EQ:
                    case frontend::Opcode::IF_NE: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X9);
                        Arm64Reg r2 = load_op(insn->src2, Arm64Reg::X10);
                        e.cmp_reg_reg(r1, r2);
                        uint8_t cond = 0xA; // GE
                        if (insn->op == frontend::Opcode::IF_LT) cond = 0xB;
                        else if (insn->op == frontend::Opcode::IF_EQ) cond = 0x0;
                        else if (insn->op == frontend::Opcode::IF_NE) cond = 0x1;

                        int32_t delta = 2;
                        if (is_pass2 && insn->target_label) {
                            size_t target_w = get_label_offset(insn->target_label);
                            size_t current_w = (e.code_size() / 4); // b_cond is emitted at current_w
                            delta = static_cast<int32_t>(target_w) - static_cast<int32_t>(current_w);
                        }
                        e.b_cond(cond, delta);
                        break;
                    }
                    case frontend::Opcode::GOTO: {
                        int32_t delta = -5;
                        if (is_pass2 && insn->target_label) {
                            size_t target_w = get_label_offset(insn->target_label);
                            size_t current_w = (e.code_size() / 4); // b_uncond is emitted at current_w
                            delta = static_cast<int32_t>(target_w) - static_cast<int32_t>(current_w);
                        }
                        e.b_uncond(delta);
                        break;
                    }
                    case frontend::Opcode::STR_LEN: {
                        e.mov_reg_imm64(Arm64Reg::X9, insn->string_len);
                        store_reg(insn->dest.reg, Arm64Reg::X9);
                        break;
                    }
                    case frontend::Opcode::RETURN_VAL: {
                        Arm64Reg r1 = load_op(insn->src1, Arm64Reg::X0);
                        if (r1 != Arm64Reg::X0) e.mov_reg_reg(Arm64Reg::X0, r1);
                        e.add_sp_imm32(320);
                        e.pop_fp_lr();
                        e.ret();
                        break;
                    }
                    case frontend::Opcode::RETURN_VOID: {
                        e.add_sp_imm32(320);
                        e.pop_fp_lr();
                        e.ret();
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    };

    // Pass 1: Measure basic block label word offsets
    AArch64Encoder temp_enc;
    emit_code(temp_enc, false);

    // Pass 2: Emit final code with exact relative branch offsets
    AArch64Encoder enc;
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

bool AArch64TargetBackend::compile_to_elf(frontend::Program* prog, const char* out_filename) {
    if (!prog || !out_filename) return false;

    ElfEmitter* elf = new ElfEmitter();
    elf->set_machine_arch(183); // EM_AARCH64
    SimpleByteBuffer* text_buf = new SimpleByteBuffer();

    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        uint64_t func_offset = text_buf->size();

        AArch64Encoder enc;
        enc.push_fp_lr();
        enc.mov_fp_sp();

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::ADD_I64:
                    case frontend::Opcode::ADD_I32:
                        enc.add_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                        break;
                    case frontend::Opcode::SUB_I64:
                    case frontend::Opcode::SUB_I32:
                        enc.sub_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                        break;
                    case frontend::Opcode::MUL_I32:
                        enc.mul_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                        break;
                    case frontend::Opcode::CONST_STRING: {
                        uint64_t ro_off = elf->append_rodata(insn->string_val, insn->string_len + 1);
                        uint64_t insn_off = func_offset + enc.code_size();

                        int32_t text_page = static_cast<int32_t>(insn_off >> 12);
                        int32_t rodata_page = static_cast<int32_t>((text_buf->size() + enc.code_size() + ro_off) >> 12);
                        int32_t page_disp = rodata_page - text_page;
                        uint16_t page_offset = static_cast<uint16_t>(ro_off & 0xFFF);

                        enc.adrp(Arm64Reg::X0, page_disp);
                        enc.add_imm12(Arm64Reg::X0, Arm64Reg::X0, page_offset);

                        elf->add_relocation(insn_off, 2 /* .rodata */, 275 /* R_AARCH64_ADR_PREL_PG_HI21 */, static_cast<int64_t>(ro_off));
                        elf->add_relocation(insn_off + 4, 2 /* .rodata */, 277 /* R_AARCH64_ADD_ABS_LO12_NC */, static_cast<int64_t>(ro_off));
                        break;
                    }
                    case frontend::Opcode::RETURN_VAL:
                    case frontend::Opcode::RETURN_VOID:
                        enc.pop_fp_lr();
                        enc.ret();
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

    // Write ELF object using EM_AARCH64 (183) machine type
    bool success = elf->write_elf_object(out_filename, text_buf->data(), text_buf->size());
    delete text_buf;
    delete elf;
    return success;
}

} // namespace backend
} // namespace ana
