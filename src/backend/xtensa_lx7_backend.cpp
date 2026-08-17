#include "xtensa_lx7_backend.h"
#include "vmem_provider.h"
#include "elf_emitter.h"
#include "../frontend/ana_ast.h"

namespace ana {
namespace backend {

XtensaLX7Encoder::XtensaLX7Encoder() : buffer_(nullptr), capacity_(4096), size_(0) {
    void* ptr = sys::raw_mmap(nullptr, capacity_, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (ptr && ptr != (void*)-1) {
        buffer_ = static_cast<uint8_t*>(ptr);
    } else {
        buffer_ = nullptr;
        capacity_ = 0;
    }
}

XtensaLX7Encoder::~XtensaLX7Encoder() {
    if (buffer_ && buffer_ != (uint8_t*)-1 && capacity_ > 0) {
        sys::raw_munmap(buffer_, capacity_);
        buffer_ = nullptr;
    }
}

void XtensaLX7Encoder::reset() {
    size_ = 0;
}

void XtensaLX7Encoder::emit8(uint8_t byte) {
    if (!buffer_) return;
    if (size_ + 1 > capacity_) {
        size_t new_cap = capacity_ * 2;
        void* new_buf = sys::raw_mmap(nullptr, new_cap, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
        if (new_buf && new_buf != (void*)-1) {
            sys::freestanding_memcpy(new_buf, buffer_, size_);
            sys::raw_munmap(buffer_, capacity_);
            buffer_ = static_cast<uint8_t*>(new_buf);
            capacity_ = new_cap;
        } else {
            return;
        }
    }
    buffer_[size_++] = byte;
}

void XtensaLX7Encoder::emit16(uint16_t insn16) {
    emit8(static_cast<uint8_t>(insn16 & 0xFF));
    emit8(static_cast<uint8_t>((insn16 >> 8) & 0xFF));
}

void XtensaLX7Encoder::emit24(uint32_t insn24) {
    emit8(static_cast<uint8_t>(insn24 & 0xFF));
    emit8(static_cast<uint8_t>((insn24 >> 8) & 0xFF));
    emit8(static_cast<uint8_t>((insn24 >> 16) & 0xFF));
}

// 24-bit RRR Format: [op2:4][r:4][s:4][t:4][op1:4][op0:4]
static inline uint32_t encode_rrr(uint8_t op0, uint8_t op1, uint8_t op2, uint8_t r, uint8_t s, uint8_t t) {
    return (static_cast<uint32_t>(op0) & 0xF) |
          ((static_cast<uint32_t>(op1) & 0xF) << 4) |
          ((static_cast<uint32_t>(t) & 0xF) << 8) |
          ((static_cast<uint32_t>(s) & 0xF) << 12) |
          ((static_cast<uint32_t>(r) & 0xF) << 16) |
          ((static_cast<uint32_t>(op2) & 0xF) << 20);
}

void XtensaLX7Encoder::add_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 0, 0, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::sub_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 0, 1, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::mull_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 2, 8, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::quos_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 2, 12, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::and_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 0, 10, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::or_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 0, 11, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::xor_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 0, 12, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::slli_reg_imm(XtensaReg rr, XtensaReg rs, uint8_t sa) {
    uint8_t shift = (32 - (sa & 0x1F)) & 0x1F;
    emit24(encode_rrr(0, 1, 0, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), shift & 0xF));
}

void XtensaLX7Encoder::srai_reg_imm(XtensaReg rr, XtensaReg rt, uint8_t sa) {
    emit24(encode_rrr(0, 1, 2, static_cast<uint8_t>(rr), sa & 0xF, static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::srli_reg_imm(XtensaReg rr, XtensaReg rt, uint8_t sa) {
    emit24(encode_rrr(0, 1, 4, static_cast<uint8_t>(rr), sa & 0xF, static_cast<uint8_t>(rt)));
}

// 24-bit Add Immediate (ADDI): [imm8][r=0xC][s][t][op0=2]
void XtensaLX7Encoder::addi_reg_imm(XtensaReg rt, XtensaReg rs, int8_t imm8) {
    uint32_t insn = (0x2) |
                   (0xC << 4) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 8) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(imm8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::movi_reg_imm(XtensaReg rt, int32_t imm12) {
    int8_t low8 = static_cast<int8_t>(imm12 & 0xFF);
    uint8_t high4 = static_cast<uint8_t>((imm12 >> 8) & 0xF);
    uint32_t insn = (0x2) |
                   (0xA << 4) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 8) |
                   ((static_cast<uint32_t>(high4 & 0xF)) << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(low8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::l32i_reg_mem(XtensaReg rt, XtensaReg rs, uint16_t offset8_words) {
    uint32_t insn = (0x2) |
                   (0x2 << 4) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 8) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 12) |
                   ((static_cast<uint32_t>(offset8_words & 0xFF)) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::s32i_reg_mem(XtensaReg rt, XtensaReg rs, uint16_t offset8_words) {
    uint32_t insn = (0x2) |
                   (0x6 << 4) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 8) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 12) |
                   ((static_cast<uint32_t>(offset8_words & 0xFF)) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::l32r_pc_rel(XtensaReg rt, int32_t word_offset16) {
    uint16_t off16 = static_cast<uint16_t>(word_offset16 & 0xFFFF);
    uint32_t insn = (0x1) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 4) |
                   ((static_cast<uint32_t>(off16)) << 8);
    emit24(insn);
}

// Single-Precision Floating Point (FPU Extension)
void XtensaLX7Encoder::add_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft) {
    emit24(encode_rrr(0, 10, 0, static_cast<uint8_t>(fr), static_cast<uint8_t>(fs), static_cast<uint8_t>(ft)));
}

void XtensaLX7Encoder::sub_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft) {
    emit24(encode_rrr(0, 10, 1, static_cast<uint8_t>(fr), static_cast<uint8_t>(fs), static_cast<uint8_t>(ft)));
}

void XtensaLX7Encoder::mul_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft) {
    emit24(encode_rrr(0, 10, 2, static_cast<uint8_t>(fr), static_cast<uint8_t>(fs), static_cast<uint8_t>(ft)));
}

void XtensaLX7Encoder::div_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft) {
    emit24(encode_rrr(0, 10, 3, static_cast<uint8_t>(fr), static_cast<uint8_t>(fs), static_cast<uint8_t>(ft)));
}

// ESP32-S3 DSP SIMD TIE Extension
void XtensaLX7Encoder::ee_vadd_s32(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(4, 0, 0, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::ee_vmul_s32(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(4, 1, 0, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

// Control Flow
void XtensaLX7Encoder::beq_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8) {
    uint32_t insn = (0x7) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 4) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 8) |
                   (0x1 << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(offset8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::bne_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8) {
    uint32_t insn = (0x7) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 4) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 8) |
                   (0x9 << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(offset8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::blt_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8) {
    uint32_t insn = (0x7) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 4) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 8) |
                   (0x2 << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(offset8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::bge_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8) {
    uint32_t insn = (0x7) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 4) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 8) |
                   (0xA << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(offset8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::call0_offset(int32_t offset18_words) {
    uint32_t off18 = static_cast<uint32_t>(offset18_words & 0x3FFFF);
    uint32_t insn = (0x5) | (off18 << 6);
    emit24(insn);
}

void XtensaLX7Encoder::callx0_reg(XtensaReg rs) {
    emit24(encode_rrr(0, 0, 0, 0, static_cast<uint8_t>(rs), 0));
}

void XtensaLX7Encoder::ret_call0() {
    emit16(0xF00D); // RET.N
}

void XtensaLX7Encoder::nop() {
    emit24(0x0020F0); // NOP
}

void XtensaLX7Encoder::mov_reg_imm32(XtensaReg rt, uint32_t val) {
    int32_t imm12 = static_cast<int32_t>(val);
    if (imm12 >= -2048 && imm12 <= 2047) {
        movi_reg_imm(rt, imm12);
    } else {
        uint32_t high16 = (val >> 16) & 0xFFFF;
        uint32_t low16 = val & 0xFFFF;
        movi_reg_imm(rt, static_cast<int32_t>(static_cast<int16_t>(high16)));
        slli_reg_imm(rt, rt, 16);
        if (low16 != 0) {
            movi_reg_imm(XtensaReg::A10, static_cast<int32_t>(static_cast<int16_t>(low16)));
            add_reg_reg(rt, rt, XtensaReg::A10);
        }
    }
}

void XtensaLX7Encoder::mov_reg_reg(XtensaReg rt, XtensaReg rs) {
    add_reg_reg(rt, rs, XtensaReg::A0);
}

void XtensaLX7Encoder::push_frame(uint32_t bytes) {
    int32_t stack_disp = -static_cast<int32_t>((bytes + 15) & ~15);
    addi_reg_imm(XtensaReg::A1, XtensaReg::A1, static_cast<int8_t>(stack_disp));
    s32i_reg_mem(XtensaReg::A0, XtensaReg::A1, 0);
}

void XtensaLX7Encoder::pop_frame(uint32_t bytes) {
    l32i_reg_mem(XtensaReg::A0, XtensaReg::A1, 0);
    int32_t stack_disp = static_cast<int32_t>((bytes + 15) & ~15);
    addi_reg_imm(XtensaReg::A1, XtensaReg::A1, static_cast<int8_t>(stack_disp));
}

static inline XtensaReg op_to_xtensa(const frontend::Operand& op) {
    if (op.kind == frontend::OperandKind::REGISTER) {
        if (op.reg.type == frontend::RegisterType::PARAM) {
            switch (op.reg.index) {
                case 0: return XtensaReg::A2;
                case 1: return XtensaReg::A3;
                case 2: return XtensaReg::A4;
                case 3: return XtensaReg::A5;
                case 4: return XtensaReg::A6;
                case 5: return XtensaReg::A7;
                default: return XtensaReg::A2;
            }
        } else { // LOCAL (v0..vN)
            switch (op.reg.index) {
                case 0: return XtensaReg::A2;
                case 1: return XtensaReg::A3;
                case 2: return XtensaReg::A4;
                case 3: return XtensaReg::A5;
                case 4: return XtensaReg::A6;
                case 5: return XtensaReg::A7;
                case 6: return XtensaReg::A8;
                case 7: return XtensaReg::A9;
                case 8: return XtensaReg::A10;
                case 9: return XtensaReg::A11;
                default: return XtensaReg::A2;
            }
        }
    }
    return XtensaReg::A2;
}

static void lower_xtensa_insn(XtensaLX7Encoder& enc, frontend::Instruction* insn, bool is_baremetal) {
    if (!insn) return;
    switch (insn->op) {
        case frontend::Opcode::MOVE_CONST:
            if (insn->src1.kind == frontend::OperandKind::CONST_INT) {
                enc.mov_reg_imm32(op_to_xtensa(insn->dest), static_cast<uint32_t>(insn->src1.const_val));
            }
            break;
        case frontend::Opcode::MOVE:
            enc.mov_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1));
            break;
        case frontend::Opcode::ADD_I32:
        case frontend::Opcode::ADD_I64:
            enc.add_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1), op_to_xtensa(insn->src2));
            break;
        case frontend::Opcode::SUB_I32:
        case frontend::Opcode::SUB_I64:
            enc.sub_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1), op_to_xtensa(insn->src2));
            break;
        case frontend::Opcode::MUL_I32:
        case frontend::Opcode::MUL_I64:
            enc.mull_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1), op_to_xtensa(insn->src2));
            break;
        case frontend::Opcode::DIV_I32:
        case frontend::Opcode::DIV_I64:
            enc.quos_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1), op_to_xtensa(insn->src2));
            break;
        case frontend::Opcode::AND_I32:
        case frontend::Opcode::AND_I64:
            enc.and_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1), op_to_xtensa(insn->src2));
            break;
        case frontend::Opcode::OR_I32:
        case frontend::Opcode::OR_I64:
            enc.or_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1), op_to_xtensa(insn->src2));
            break;
        case frontend::Opcode::XOR_I32:
        case frontend::Opcode::XOR_I64:
            enc.xor_reg_reg(op_to_xtensa(insn->dest), op_to_xtensa(insn->src1), op_to_xtensa(insn->src2));
            break;
        case frontend::Opcode::STORE_MEM:
            if (insn->dest.kind == frontend::OperandKind::MEM_OFFSET) {
                XtensaReg base_reg = (insn->dest.mem.base.type == frontend::RegisterType::PARAM) ?
                    XtensaReg::A2 : op_to_xtensa(frontend::Operand::make_reg(insn->dest.mem.base.type, insn->dest.mem.base.index));
                XtensaReg val_reg = op_to_xtensa(insn->src1);
                enc.s32i_reg_mem(val_reg, base_reg, static_cast<uint16_t>(insn->dest.mem.offset / 4));
            }
            break;
        case frontend::Opcode::LOAD_MEM:
            if (insn->src1.kind == frontend::OperandKind::MEM_OFFSET) {
                XtensaReg base_reg = (insn->src1.mem.base.type == frontend::RegisterType::PARAM) ?
                    XtensaReg::A2 : op_to_xtensa(frontend::Operand::make_reg(insn->src1.mem.base.type, insn->src1.mem.base.index));
                XtensaReg dest_reg = op_to_xtensa(insn->dest);
                enc.l32i_reg_mem(dest_reg, base_reg, static_cast<uint16_t>(insn->src1.mem.offset / 4));
            }
            break;
        case frontend::Opcode::GOTO:
            enc.emit24(0x000006); // J . (self loop)
            break;
        case frontend::Opcode::ADD_FLOAT_32:
            enc.add_s(XtensaFpReg::F0, XtensaFpReg::F0, XtensaFpReg::F1);
            break;
        case frontend::Opcode::SUB_FLOAT_64:
            enc.sub_s(XtensaFpReg::F0, XtensaFpReg::F0, XtensaFpReg::F1);
            break;
        case frontend::Opcode::MUL_FLOAT_64:
            enc.mul_s(XtensaFpReg::F0, XtensaFpReg::F0, XtensaFpReg::F1);
            break;
        case frontend::Opcode::DIV_FLOAT_64:
            enc.div_s(XtensaFpReg::F0, XtensaFpReg::F0, XtensaFpReg::F1);
            break;
        case frontend::Opcode::ADD_VECTOR_I32X4:
            enc.ee_vadd_s32(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
            break;
        case frontend::Opcode::MUL_VECTOR_I32X8:
            enc.ee_vmul_s32(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
            break;
        case frontend::Opcode::RETURN_VAL:
        case frontend::Opcode::RETURN_VOID:
            if (is_baremetal) {
                enc.emit24(0x000006); // J . (park CPU)
            } else {
                enc.pop_frame(32);
                enc.ret_call0();
            }
            break;
        default:
            enc.nop();
            break;
    }
}

XtensaLX7TargetBackend::XtensaLX7TargetBackend() : runtime_(nullptr) {}
XtensaLX7TargetBackend::XtensaLX7TargetBackend(AnastasiaJitRuntime* runtime) : runtime_(runtime) {}
XtensaLX7TargetBackend::~XtensaLX7TargetBackend() {}

void* XtensaLX7TargetBackend::compile_function(frontend::Function* fn, frontend::Program* prog) {
    if (!fn) return nullptr;

    XtensaLX7Encoder enc;
    enc.push_frame(32);

    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            lower_xtensa_insn(enc, insn, false);
        }
    }

    size_t sz = enc.code_size();
    if (sz == 0) return nullptr;

    void* mem = CustomVMemProvider::alloc_rw(sz);
    if (!mem) return nullptr;
    sys::freestanding_memcpy(mem, enc.code_bytes(), sz);
    CustomVMemProvider::make_rx(mem, sz);
    sys::clear_icache(mem, sz);
    return mem;
}

bool XtensaLX7TargetBackend::compile_to_elf(frontend::Program* prog, const char* out_filename) {
    if (!prog || !out_filename) return false;

    ElfEmitter elf;
    elf.set_machine_arch(EM_XTENSA);

    SimpleByteBuffer text_buf;

    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        uint64_t func_off = text_buf.size();

        XtensaLX7Encoder enc;
        enc.push_frame(32);

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                lower_xtensa_insn(enc, insn, false);
            }
        }

        size_t func_sz = enc.code_size();
        text_buf.write(enc.code_bytes(), func_sz);

        elf.add_symbol(fn->name ? fn->name : "anon_func", STB_GLOBAL, STT_FUNC, 1, func_off, func_sz);
    }

    return elf.write_elf_object(out_filename, text_buf.data(), text_buf.size());
}

bool XtensaLX7TargetBackend::compile_to_esp32_bin(frontend::Program* prog, const char* out_bin_path) {
    if (!prog || !out_bin_path) return false;

    SimpleByteBuffer text_buf;

    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        XtensaLX7Encoder enc;

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                lower_xtensa_insn(enc, insn, true);
            }
        }

        size_t func_sz = enc.code_size();
        text_buf.write(enc.code_bytes(), func_sz);
    }

    if (text_buf.size() == 0) return false;

    // ESP32-S3 Flash Image Header (24 bytes)
    uint8_t header[24];
    sys::freestanding_memset(header, 0, sizeof(header));
    header[0] = 0xE9; // Magic byte
    header[1] = 1;    // 1 segment
    header[2] = 0x02; // SPI Flash mode DIO
    header[3] = 0x20; // 40MHz, 16MB
    uint32_t entry_addr = 0x40370000;
    *reinterpret_cast<uint32_t*>(&header[4]) = entry_addr;
    header[8] = 0xEE; // WP Pin
    header[12] = 0x09; // Chip ID: ESP32-S3 (9 = 0x0009 at offset 12-13)
    header[13] = 0x00;

    size_t raw_code_sz = text_buf.size();
    size_t aligned_code_sz = (raw_code_sz + 3) & ~3UL;

    // Segment 0 Header (8 bytes)
    uint8_t seg_header[8];
    *reinterpret_cast<uint32_t*>(&seg_header[0]) = entry_addr;
    *reinterpret_cast<uint32_t*>(&seg_header[4]) = static_cast<uint32_t>(aligned_code_sz);

    // Calculate exact 8-bit XOR Checksum starting at 0xEF over aligned payload
    uint8_t checksum = 0xEF;
    const uint8_t* code_bytes = text_buf.data();
    for (size_t i = 0; i < raw_code_sz; i++) {
        checksum ^= code_bytes[i];
    }

    int fd = sys::raw_open(out_bin_path, 577 /* O_WRONLY|O_CREAT|O_TRUNC */, 0666);
    if (fd < 0) return false;

    sys::raw_write(fd, header, sizeof(header));
    sys::raw_write(fd, seg_header, sizeof(seg_header));
    sys::raw_write(fd, text_buf.data(), raw_code_sz);

    // Write 4-byte alignment padding for segment if needed
    uint8_t zero_byte = 0;
    for (size_t i = raw_code_sz; i < aligned_code_sz; i++) {
        sys::raw_write(fd, &zero_byte, 1);
    }

    // Pad file so that (file_size % 16) == 15, then append the checksum byte
    size_t payload_len = sizeof(header) + sizeof(seg_header) + aligned_code_sz;
    size_t pad_len = (15 - (payload_len % 16)) % 16;
    for (size_t i = 0; i < pad_len; i++) {
        sys::raw_write(fd, &zero_byte, 1);
    }
    sys::raw_write(fd, &checksum, 1);

    sys::raw_close(fd);
    return true;
}

} // namespace backend
} // namespace ana
