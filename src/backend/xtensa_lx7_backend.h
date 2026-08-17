#ifndef XTENSA_LX7_BACKEND_H
#define XTENSA_LX7_BACKEND_H

#include "ana_target_backend.h"
#include "vmem_provider.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

// Cadence Tensilica Xtensa LX7 Physical Registers (Call0 ABI)
enum class XtensaReg : uint8_t {
    A0  = 0,   // Return Address
    A1  = 1,   // Stack Pointer (SP)
    A2  = 2,   // Argument 0 / Return Value 0
    A3  = 3,   // Argument 1 / Return Value 1
    A4  = 4,   // Argument 2
    A5  = 5,   // Argument 3
    A6  = 6,   // Argument 4
    A7  = 7,   // Argument 5
    A8  = 8,   // Caller-Saved Scratch 0
    A9  = 9,   // Caller-Saved Scratch 1
    A10 = 10,  // Caller-Saved Scratch 2
    A11 = 11,  // Caller-Saved Scratch 3
    A12 = 12,  // Callee-Saved Local 0 / Frame Pointer
    A13 = 13,  // Callee-Saved Local 1
    A14 = 14,  // Callee-Saved Local 2
    A15 = 15   // Callee-Saved Local 3
};

// Single-Precision Floating-Point Registers (FPU Extension)
enum class XtensaFpReg : uint8_t {
    F0 = 0,   F1 = 1,   F2 = 2,   F3 = 3,
    F4 = 4,   F5 = 5,   F6 = 6,   F7 = 7,
    F8 = 8,   F9 = 9,   F10 = 10, F11 = 11,
    F12 = 12, F13 = 13, F14 = 14, F15 = 15
};

// Bare-Metal Tensilica Xtensa LX7 24-bit / 16-bit Instruction Emitter
class XtensaLX7Encoder {
public:
    XtensaLX7Encoder();
    ~XtensaLX7Encoder();

    void reset();

    void emit24(uint32_t insn24);
    void emit16(uint16_t insn16);
    void emit8(uint8_t byte);

    // RRR-type Encodings (OP0 = 0x0)
    void add_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void sub_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void mull_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void quos_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void and_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void or_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void xor_reg_reg(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void slli_reg_imm(XtensaReg rr, XtensaReg rs, uint8_t sa);
    void srai_reg_imm(XtensaReg rr, XtensaReg rt, uint8_t sa);
    void srli_reg_imm(XtensaReg rr, XtensaReg rt, uint8_t sa);

    // Immediate & Load/Store Encodings
    void addi_reg_imm(XtensaReg rt, XtensaReg rs, int8_t imm8);
    void movi_reg_imm(XtensaReg rt, int32_t imm12);
    void l32i_reg_mem(XtensaReg rt, XtensaReg rs, uint16_t offset8_words);
    void s32i_reg_mem(XtensaReg rt, XtensaReg rs, uint16_t offset8_words);
    void l32r_pc_rel(XtensaReg rt, int32_t word_offset16);

    // Single-Precision Floating Point (FPU Extension)
    void add_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft);
    void sub_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft);
    void mul_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft);
    void div_s(XtensaFpReg fr, XtensaFpReg fs, XtensaFpReg ft);

    // ESP32-S3 / DSP SIMD TIE Extension
    void ee_vadd_s32(XtensaReg rr, XtensaReg rs, XtensaReg rt);
    void ee_vmul_s32(XtensaReg rr, XtensaReg rs, XtensaReg rt);

    // Control Flow Encodings
    void beq_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8);
    void bne_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8);
    void blt_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8);
    void bge_reg_reg(XtensaReg rs, XtensaReg rt, int32_t offset8);
    void call0_offset(int32_t offset18_words);
    void callx0_reg(XtensaReg rs);
    void ret_call0();
    void nop();

    // Composite High-Level Encodings
    void mov_reg_imm32(XtensaReg rt, uint32_t val);
    void mov_reg_reg(XtensaReg rt, XtensaReg rs);
    void push_frame(uint32_t bytes);
    void pop_frame(uint32_t bytes);

    const uint8_t* code_bytes() const { return buffer_; }
    size_t code_size() const { return size_; }

private:
    uint8_t* buffer_;
    size_t capacity_;
    size_t size_;
};

class AnastasiaJitRuntime;

class XtensaLX7TargetBackend : public AnaTargetBackend {
public:
    XtensaLX7TargetBackend();
    explicit XtensaLX7TargetBackend(AnastasiaJitRuntime* runtime);
    virtual ~XtensaLX7TargetBackend();

    void set_runtime(AnastasiaJitRuntime* runtime) { runtime_ = runtime; }
    virtual TargetArch arch() const override { return TargetArch::XTENSA_LX7; }
    virtual void* compile_function(frontend::Function* fn, frontend::Program* prog) override;
    virtual bool compile_to_elf(frontend::Program* prog, const char* out_filename) override;

private:
    AnastasiaJitRuntime* runtime_;
};

} // namespace backend
} // namespace ana

#endif // XTENSA_LX7_BACKEND_H
