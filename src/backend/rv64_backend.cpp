#include "rv64_backend.h"
#include "elf_emitter.h"
#include "ana_lowerer.h"
#include "../sys/object_heap.h"
#include "jit_string_pool.h"
#include <cstdlib>

namespace ana {
namespace backend {

static inline uint8_t rv64_u8(Rv64Reg r) {
    return static_cast<uint8_t>(r);
}

Rv64Encoder::Rv64Encoder() : buffer_(nullptr), capacity_(0), size_(0) {
    capacity_ = 256;
    buffer_ = static_cast<uint32_t*>(malloc(capacity_ * sizeof(uint32_t)));
}

Rv64Encoder::~Rv64Encoder() {
    if (buffer_) free(buffer_);
}

void Rv64Encoder::reset() {
    size_ = 0;
}

void Rv64Encoder::emit32(uint32_t code) {
    if (size_ >= capacity_) {
        capacity_ *= 2;
        buffer_ = static_cast<uint32_t*>(realloc(buffer_, capacity_ * sizeof(uint32_t)));
    }
    buffer_[size_++] = code;
}

// R-type Encoding: [funct7 7][rs2 5][rs1 5][funct3 3][rd 5][opcode 7]
static inline uint32_t encode_r(uint8_t funct7, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
    return (static_cast<uint32_t>(funct7 & 0x7F) << 25) |
           (static_cast<uint32_t>(rs2 & 0x1F) << 20) |
           (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
           (static_cast<uint32_t>(funct3 & 0x7) << 12) |
           (static_cast<uint32_t>(rd & 0x1F) << 7) |
           (static_cast<uint32_t>(opcode & 0x7F));
}

// I-type Encoding: [imm12 12][rs1 5][funct3 3][rd 5][opcode 7]
static inline uint32_t encode_i(int32_t imm12, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
    return (static_cast<uint32_t>(imm12 & 0xFFF) << 20) |
           (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
           (static_cast<uint32_t>(funct3 & 0x7) << 12) |
           (static_cast<uint32_t>(rd & 0x1F) << 7) |
           (static_cast<uint32_t>(opcode & 0x7F));
}

// S-type Encoding: [imm12[11:5] 7][rs2 5][rs1 5][funct3 3][imm12[4:0] 5][opcode 7]
static inline uint32_t encode_s(int32_t imm12, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t opcode) {
    uint32_t imm = static_cast<uint32_t>(imm12);
    return ((imm >> 5) & 0x7F) << 25 |
           (static_cast<uint32_t>(rs2 & 0x1F) << 20) |
           (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
           (static_cast<uint32_t>(funct3 & 0x7) << 12) |
           ((imm & 0x1F) << 7) |
           (static_cast<uint32_t>(opcode & 0x7F));
}

// B-type Encoding: [imm[12|10:5] 7][rs2 5][rs1 5][funct3 3][imm[4:1|11] 5][opcode 7]
static inline uint32_t encode_b(int32_t offset, uint8_t rs1, uint8_t rs2, uint8_t funct3, uint8_t opcode) {
    uint32_t imm = static_cast<uint32_t>(offset);
    uint32_t imm12 = (imm >> 12) & 1;
    uint32_t imm11 = (imm >> 11) & 1;
    uint32_t imm10_5 = (imm >> 5) & 0x3F;
    uint32_t imm4_1 = (imm >> 1) & 0xF;

    return (imm12 << 31) |
           (imm10_5 << 25) |
           (static_cast<uint32_t>(rs2 & 0x1F) << 20) |
           (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
           (static_cast<uint32_t>(funct3 & 0x7) << 12) |
           (imm4_1 << 8) |
           (imm11 << 7) |
           (static_cast<uint32_t>(opcode & 0x7F));
}

// U-type Encoding: [imm20 20][rd 5][opcode 7]
static inline uint32_t encode_u(int32_t imm20, uint8_t rd, uint8_t opcode) {
    return (static_cast<uint32_t>(imm20 & 0xFFFFF) << 12) |
           (static_cast<uint32_t>(rd & 0x1F) << 7) |
           (static_cast<uint32_t>(opcode & 0x7F));
}

// J-type Encoding: [imm[20|10:1|11|19:12] 20][rd 5][opcode 7]
static inline uint32_t encode_j(int32_t offset, uint8_t rd, uint8_t opcode) {
    uint32_t imm = static_cast<uint32_t>(offset);
    uint32_t imm20 = (imm >> 20) & 1;
    uint32_t imm19_12 = (imm >> 12) & 0xFF;
    uint32_t imm11 = (imm >> 11) & 1;
    uint32_t imm10_1 = (imm >> 1) & 0x3FF;

    return (imm20 << 31) |
           (imm10_1 << 21) |
           (imm11 << 20) |
           (imm19_12 << 12) |
           (static_cast<uint32_t>(rd & 0x1F) << 7) |
           (static_cast<uint32_t>(opcode & 0x7F));
}

void Rv64Encoder::add_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x00, rv64_u8(rs2), rv64_u8(rs1), 0x0, rv64_u8(rd), 0x33));
}

void Rv64Encoder::sub_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x20, rv64_u8(rs2), rv64_u8(rs1), 0x0, rv64_u8(rd), 0x33));
}

void Rv64Encoder::mul_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x01, rv64_u8(rs2), rv64_u8(rs1), 0x0, rv64_u8(rd), 0x33));
}

void Rv64Encoder::div_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x01, rv64_u8(rs2), rv64_u8(rs1), 0x4, rv64_u8(rd), 0x33));
}

void Rv64Encoder::and_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x00, rv64_u8(rs2), rv64_u8(rs1), 0x7, rv64_u8(rd), 0x33));
}

void Rv64Encoder::or_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x00, rv64_u8(rs2), rv64_u8(rs1), 0x6, rv64_u8(rd), 0x33));
}

void Rv64Encoder::xor_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x00, rv64_u8(rs2), rv64_u8(rs1), 0x4, rv64_u8(rd), 0x33));
}

void Rv64Encoder::sll_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x00, rv64_u8(rs2), rv64_u8(rs1), 0x1, rv64_u8(rd), 0x33));
}

void Rv64Encoder::srl_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2) {
    emit32(encode_r(0x00, rv64_u8(rs2), rv64_u8(rs1), 0x5, rv64_u8(rd), 0x33));
}

void Rv64Encoder::addi_reg_imm(Rv64Reg rd, Rv64Reg rs1, int32_t imm12) {
    emit32(encode_i(imm12, rv64_u8(rs1), 0x0, rv64_u8(rd), 0x13));
}

void Rv64Encoder::ld_reg_mem(Rv64Reg rd, Rv64Reg rs1, int32_t offset12) {
    emit32(encode_i(offset12, rv64_u8(rs1), 0x3, rv64_u8(rd), 0x03));
}

void Rv64Encoder::jalr_reg(Rv64Reg rd, Rv64Reg rs1, int32_t offset12) {
    emit32(encode_i(offset12, rv64_u8(rs1), 0x0, rv64_u8(rd), 0x67));
}

void Rv64Encoder::sd_reg_mem(Rv64Reg rs2, Rv64Reg rs1, int32_t offset12) {
    emit32(encode_s(offset12, rv64_u8(rs2), rv64_u8(rs1), 0x3, 0x23));
}

void Rv64Encoder::lui_reg_imm20(Rv64Reg rd, int32_t imm20) {
    emit32(encode_u(imm20, rv64_u8(rd), 0x37));
}

void Rv64Encoder::beq_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset) {
    emit32(encode_b(offset, rv64_u8(rs1), rv64_u8(rs2), 0x0, 0x63));
}

void Rv64Encoder::bne_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset) {
    emit32(encode_b(offset, rv64_u8(rs1), rv64_u8(rs2), 0x1, 0x63));
}

void Rv64Encoder::blt_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset) {
    emit32(encode_b(offset, rv64_u8(rs1), rv64_u8(rs2), 0x4, 0x63));
}

void Rv64Encoder::bge_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset) {
    emit32(encode_b(offset, rv64_u8(rs1), rv64_u8(rs2), 0x5, 0x63));
}

void Rv64Encoder::jal_reg(Rv64Reg rd, int32_t offset) {
    emit32(encode_j(offset, rv64_u8(rd), 0x6F));
}

void Rv64Encoder::mov_reg_imm64(Rv64Reg rd, uint64_t val) {
    int32_t hi20 = static_cast<int32_t>((val + 0x800) >> 12) & 0xFFFFF;
    int32_t lo12 = static_cast<int32_t>(val & 0xFFF);
    if (lo12 & 0x800) lo12 |= 0xFFFFF000;

    lui_reg_imm20(rd, hi20);
    if (lo12 != 0 || val == 0) {
        addi_reg_imm(rd, rd, lo12);
    }
}

void Rv64Encoder::mov_reg_reg(Rv64Reg rd, Rv64Reg rs1) {
    addi_reg_imm(rd, rs1, 0);
}

void Rv64Encoder::push_fp_ra() {
    addi_reg_imm(Rv64Reg::SP, Rv64Reg::SP, -16);
    sd_reg_mem(Rv64Reg::RA, Rv64Reg::SP, 8);
    sd_reg_mem(Rv64Reg::FP, Rv64Reg::SP, 0);
    mov_reg_reg(Rv64Reg::FP, Rv64Reg::SP);
}

void Rv64Encoder::pop_fp_ra() {
    ld_reg_mem(Rv64Reg::FP, Rv64Reg::SP, 0);
    ld_reg_mem(Rv64Reg::RA, Rv64Reg::SP, 8);
    addi_reg_imm(Rv64Reg::SP, Rv64Reg::SP, 16);
}

void Rv64Encoder::ret() {
    jalr_reg(Rv64Reg::ZERO, Rv64Reg::RA, 0);
}

void Rv64Encoder::nop() {
    addi_reg_imm(Rv64Reg::ZERO, Rv64Reg::ZERO, 0);
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

Rv64TargetBackend::Rv64TargetBackend() : runtime_(nullptr) {}
Rv64TargetBackend::Rv64TargetBackend(AnastasiaJitRuntime* runtime) : runtime_(runtime) {}
Rv64TargetBackend::~Rv64TargetBackend() {}

void* Rv64TargetBackend::compile_function(frontend::Function* fn, frontend::Program* prog) {
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

    auto emit_code = [&](Rv64Encoder& e, bool is_pass2) {
        e.push_fp_ra();
        e.addi_reg_imm(Rv64Reg::SP, Rv64Reg::SP, -256); // 256 bytes spill slots

        // Save incoming parameters A0..A7 to stack spill slots
        for (uint32_t i = 0; i < 8; ++i) {
            Rv64Reg preg = static_cast<Rv64Reg>(static_cast<uint8_t>(Rv64Reg::A0) + i);
            e.sd_reg_mem(preg, Rv64Reg::SP, 192 + i * 8);
        }

        auto load_op = [&](const frontend::Operand& op, Rv64Reg scratch) -> Rv64Reg {
            if (op.kind == frontend::OperandKind::CONST_INT) {
                e.mov_reg_imm64(scratch, static_cast<uint64_t>(op.const_val));
                return scratch;
            } else if (op.kind == frontend::OperandKind::REGISTER) {
                if (op.reg.type == frontend::RegisterType::PARAM) {
                    uint32_t offset = 192 + (op.reg.index & 7) * 8;
                    e.ld_reg_mem(scratch, Rv64Reg::SP, offset);
                    return scratch;
                } else {
                    uint32_t offset = (op.reg.index & 23) * 8;
                    e.ld_reg_mem(scratch, Rv64Reg::SP, offset);
                    return scratch;
                }
            }
            return scratch;
        };

        auto store_reg = [&](const frontend::Register& reg, Rv64Reg src_reg) {
            if (reg.type == frontend::RegisterType::PARAM) {
                uint32_t offset = 192 + (reg.index & 7) * 8;
                e.sd_reg_mem(src_reg, Rv64Reg::SP, offset);
            } else {
                uint32_t offset = (reg.index & 23) * 8;
                e.sd_reg_mem(src_reg, Rv64Reg::SP, offset);
            }
        };

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            if (bb->label) {
                record_label(bb->label, e.code_size() / 4);
            }
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::MOVE_CONST: {
                        e.mov_reg_imm64(Rv64Reg::T0, static_cast<uint64_t>(insn->src1.const_val));
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::MOVE: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        store_reg(insn->dest.reg, r1);
                        break;
                    }
                    case frontend::Opcode::ADD_I64:
                    case frontend::Opcode::ADD_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.add_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::SUB_I64:
                    case frontend::Opcode::SUB_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.sub_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::NEG_I64:
                    case frontend::Opcode::NEG_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        e.sub_reg_reg(Rv64Reg::T0, Rv64Reg::ZERO, r1);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::MUL_I64:
                    case frontend::Opcode::MUL_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.mul_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::DIV_I64:
                    case frontend::Opcode::DIV_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.div_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::AND_I64:
                    case frontend::Opcode::AND_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.and_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::OR_I64:
                    case frontend::Opcode::OR_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.or_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::XOR_I64:
                    case frontend::Opcode::XOR_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.xor_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::SHL_I64:
                    case frontend::Opcode::SHL_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.sll_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::SHR_I64:
                    case frontend::Opcode::SHR_I32:
                    case frontend::Opcode::USHR_I64:
                    case frontend::Opcode::USHR_I32: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);
                        e.srl_reg_reg(Rv64Reg::T0, r1, r2);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::POPCOUNT_I64: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::A0);
                        if (r1 != Rv64Reg::A0) e.mov_reg_reg(Rv64Reg::A0, r1);

                        auto popcnt_fn = [](uint64_t v) -> uint64_t {
                            uint64_t c = 0;
                            while (v) { c += (v & 1); v >>= 1; }
                            return c;
                        };
                        e.mov_reg_imm64(Rv64Reg::T0, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(+popcnt_fn)));
                        e.jalr_reg(Rv64Reg::RA, Rv64Reg::T0, 0);
                        store_reg(insn->dest.reg, Rv64Reg::A0);
                        break;
                    }
                    case frontend::Opcode::LOAD_MEM: {
                        frontend::Operand base_op = frontend::Operand::make_reg(insn->src1.mem.base.type, insn->src1.mem.base.index);
                        Rv64Reg base = load_op(base_op, Rv64Reg::T0);
                        e.ld_reg_mem(Rv64Reg::T1, base, insn->src1.mem.offset);
                        store_reg(insn->dest.reg, Rv64Reg::T1);
                        break;
                    }
                    case frontend::Opcode::STORE_MEM: {
                        frontend::Operand base_op = frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index);
                        Rv64Reg base = load_op(base_op, Rv64Reg::T0);
                        Rv64Reg val = load_op(insn->src1, Rv64Reg::T1);
                        e.sd_reg_mem(val, base, insn->dest.mem.offset);
                        break;
                    }
                    case frontend::Opcode::CALL_VIRT:
                    case frontend::Opcode::CALL_VIRT_FAST: {
                        Rv64Reg obj = load_op(insn->src1, Rv64Reg::A0);
                        if (obj != Rv64Reg::A0) e.mov_reg_reg(Rv64Reg::A0, obj);

                        e.ld_reg_mem(Rv64Reg::T0, Rv64Reg::A0, 0); // vtable ptr
                        e.ld_reg_mem(Rv64Reg::T1, Rv64Reg::T0, insn->vtable_slot * 8); // fn ptr
                        e.jalr_reg(Rv64Reg::RA, Rv64Reg::T1, 0);

                        if (insn->dest.kind == frontend::OperandKind::REGISTER) {
                            store_reg(insn->dest.reg, Rv64Reg::A0);
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

                        e.mov_reg_imm64(Rv64Reg::A0, inst_size);
                        e.mov_reg_imm64(Rv64Reg::A1, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(vtable_ptr)));
                        e.mov_reg_imm64(Rv64Reg::A2, class_id);
                        e.mov_reg_imm64(Rv64Reg::T0, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&alloc_obj_helper)));
                        e.jalr_reg(Rv64Reg::RA, Rv64Reg::T0, 0);

                        store_reg(insn->dest.reg, Rv64Reg::A0);
                        break;
                    }
                    case frontend::Opcode::CONST_STRING: {
                        const char* str_ptr = (runtime_ && insn->string_val) ? runtime_->string_pool().get_or_intern(insn->string_val, insn->string_len, insn->string_hash) : insn->string_val;
                        e.mov_reg_imm64(Rv64Reg::T0, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(str_ptr)));
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::IF_GE:
                    case frontend::Opcode::IF_LT:
                    case frontend::Opcode::IF_EQ:
                    case frontend::Opcode::IF_NE: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::T0);
                        Rv64Reg r2 = load_op(insn->src2, Rv64Reg::T1);

                        int32_t delta_bytes = 8;
                        if (is_pass2 && insn->target_label) {
                            size_t target_w = get_label_offset(insn->target_label);
                            size_t current_w = (e.code_size() / 4);
                            delta_bytes = static_cast<int32_t>((target_w - current_w) * 4);
                        }

                        if (insn->op == frontend::Opcode::IF_EQ) e.beq_reg_reg(r1, r2, delta_bytes);
                        else if (insn->op == frontend::Opcode::IF_NE) e.bne_reg_reg(r1, r2, delta_bytes);
                        else if (insn->op == frontend::Opcode::IF_LT) e.blt_reg_reg(r1, r2, delta_bytes);
                        else if (insn->op == frontend::Opcode::IF_GE) e.bge_reg_reg(r1, r2, delta_bytes);
                        break;
                    }
                    case frontend::Opcode::GOTO: {
                        int32_t delta_bytes = -20;
                        if (is_pass2 && insn->target_label) {
                            size_t target_w = get_label_offset(insn->target_label);
                            size_t current_w = (e.code_size() / 4);
                            delta_bytes = static_cast<int32_t>((target_w - current_w) * 4);
                        }
                        e.jal_reg(Rv64Reg::ZERO, delta_bytes);
                        break;
                    }
                    case frontend::Opcode::STR_LEN: {
                        e.mov_reg_imm64(Rv64Reg::T0, insn->string_len);
                        store_reg(insn->dest.reg, Rv64Reg::T0);
                        break;
                    }
                    case frontend::Opcode::RETURN_VAL: {
                        Rv64Reg r1 = load_op(insn->src1, Rv64Reg::A0);
                        if (r1 != Rv64Reg::A0) e.mov_reg_reg(Rv64Reg::A0, r1);
                        e.addi_reg_imm(Rv64Reg::SP, Rv64Reg::SP, 256);
                        e.pop_fp_ra();
                        e.ret();
                        break;
                    }
                    case frontend::Opcode::RETURN_VOID: {
                        e.addi_reg_imm(Rv64Reg::SP, Rv64Reg::SP, 256);
                        e.pop_fp_ra();
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
    Rv64Encoder temp_enc;
    emit_code(temp_enc, false);

    // Pass 2: Emit final code with exact relative branch offsets
    Rv64Encoder enc;
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

bool Rv64TargetBackend::compile_to_elf(frontend::Program* prog, const char* out_filename) {
    if (!prog || !out_filename) return false;

    ElfEmitter* elf = new ElfEmitter();
    elf->set_machine_arch(243); // EM_RISCV (RISC-V 64-bit)
    SimpleByteBuffer* text_buf = new SimpleByteBuffer();

    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        uint64_t func_offset = text_buf->size();

        Rv64Encoder enc;
        enc.push_fp_ra();

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::ADD_I64:
                    case frontend::Opcode::ADD_I32:
                        enc.add_reg_reg(Rv64Reg::A0, Rv64Reg::A0, Rv64Reg::A1);
                        break;
                    case frontend::Opcode::SUB_I64:
                    case frontend::Opcode::SUB_I32:
                        enc.sub_reg_reg(Rv64Reg::A0, Rv64Reg::A0, Rv64Reg::A1);
                        break;
                    case frontend::Opcode::MUL_I32:
                        enc.mul_reg_reg(Rv64Reg::A0, Rv64Reg::A0, Rv64Reg::A1);
                        break;
                    case frontend::Opcode::RETURN_VAL:
                    case frontend::Opcode::RETURN_VOID:
                        enc.pop_fp_ra();
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

    bool success = elf->write_elf_object(out_filename, text_buf->data(), text_buf->size());
    delete text_buf;
    delete elf;
    return success;
}

} // namespace backend
} // namespace ana
