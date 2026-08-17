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

// 24-bit Add Immediate (ADDI): [imm8][r=0/2][s][t][op0=2]
void XtensaLX7Encoder::addi_reg_imm(XtensaReg rt, XtensaReg rs, int8_t imm8) {
    uint32_t insn = (0x2) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 8) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(imm8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::movi_reg_imm(XtensaReg rt, int32_t imm12) {
    int8_t low8 = static_cast<int8_t>(imm12 & 0xFF);
    uint8_t high4 = static_cast<uint8_t>((imm12 >> 8) & 0xF);
    uint32_t insn = (0x2) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 8) |
                   ((static_cast<uint32_t>(high4 & 0xF)) << 12) |
                   ((static_cast<uint32_t>(static_cast<uint8_t>(low8))) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::l32i_reg_mem(XtensaReg rt, XtensaReg rs, uint16_t offset8_words) {
    uint32_t insn = (0x2) |
                   ((static_cast<uint32_t>(rt) & 0xF) << 8) |
                   ((static_cast<uint32_t>(rs) & 0xF) << 12) |
                   ((static_cast<uint32_t>(offset8_words & 0xFF)) << 16);
    emit24(insn);
}

void XtensaLX7Encoder::s32i_reg_mem(XtensaReg rt, XtensaReg rs, uint16_t offset8_words) {
    uint32_t insn = (0x2) |
                   (0x4 << 4) |
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

// ESP32-S3 / DSP SIMD TIE Extension
void XtensaLX7Encoder::ee_vadd_s32(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 14, 0, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
}

void XtensaLX7Encoder::ee_vmul_s32(XtensaReg rr, XtensaReg rs, XtensaReg rt) {
    emit24(encode_rrr(0, 14, 2, static_cast<uint8_t>(rr), static_cast<uint8_t>(rs), static_cast<uint8_t>(rt)));
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
    emit24(0x000000); // RET (or JX A0)
}

void XtensaLX7Encoder::nop() {
    emit24(0x0020F0); // NOP
}

void XtensaLX7Encoder::mov_reg_imm32(XtensaReg rt, uint32_t val) {
    int32_t imm12 = static_cast<int32_t>(val);
    if (imm12 >= -2048 && imm12 <= 2047) {
        movi_reg_imm(rt, imm12);
    } else {
        movi_reg_imm(rt, static_cast<int16_t>(val & 0xFFF));
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

XtensaLX7TargetBackend::XtensaLX7TargetBackend() : runtime_(nullptr) {}
XtensaLX7TargetBackend::XtensaLX7TargetBackend(AnastasiaJitRuntime* runtime) : runtime_(runtime) {}
XtensaLX7TargetBackend::~XtensaLX7TargetBackend() {}

void* XtensaLX7TargetBackend::compile_function(frontend::Function* fn, frontend::Program* prog) {
    if (!fn) return nullptr;

    XtensaLX7Encoder enc;
    enc.push_frame(32);

    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            switch (insn->op) {
                case frontend::Opcode::ADD_I32:
                case frontend::Opcode::ADD_I64:
                    enc.add_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
                    break;
                case frontend::Opcode::SUB_I32:
                case frontend::Opcode::SUB_I64:
                    enc.sub_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
                    break;
                case frontend::Opcode::MUL_I32:
                case frontend::Opcode::MUL_I64:
                    enc.mull_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
                    break;
                case frontend::Opcode::DIV_I32:
                case frontend::Opcode::DIV_I64:
                    enc.quos_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
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
                    enc.pop_frame(32);
                    enc.ret_call0();
                    break;
                default:
                    enc.nop();
                    break;
            }
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
                switch (insn->op) {
                    case frontend::Opcode::ADD_I32:
                    case frontend::Opcode::ADD_I64:
                        enc.add_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
                        break;
                    case frontend::Opcode::SUB_I32:
                    case frontend::Opcode::SUB_I64:
                        enc.sub_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
                        break;
                    case frontend::Opcode::MUL_I32:
                    case frontend::Opcode::MUL_I64:
                        enc.mull_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
                        break;
                    case frontend::Opcode::DIV_I32:
                    case frontend::Opcode::DIV_I64:
                        enc.quos_reg_reg(XtensaReg::A2, XtensaReg::A2, XtensaReg::A3);
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
                        enc.pop_frame(32);
                        enc.ret_call0();
                        break;
                    default:
                        enc.nop();
                        break;
                }
            }
        }

        size_t func_sz = enc.code_size();
        text_buf.write(enc.code_bytes(), func_sz);

        elf.add_symbol(fn->name ? fn->name : "anon_func", STB_GLOBAL, STT_FUNC, 1, func_off, func_sz);
    }

    return elf.write_elf_object(out_filename, text_buf.data(), text_buf.size());
}

} // namespace backend
} // namespace ana
