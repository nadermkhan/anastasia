#ifndef ARMV7_BACKEND_H
#define ARMV7_BACKEND_H

#include "ana_target_backend.h"
#include "vmem_provider.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

// ARMv7 Physical Registers (AAPCS32 ABI)
enum class Armv7Reg : uint8_t {
    R0  = 0,   R1  = 1,   R2  = 2,   R3  = 3,
    R4  = 4,   R5  = 5,   R6  = 6,   R7  = 7,
    R8  = 8,   R9  = 9,   R10 = 10,  FP  = 11, // R11 (Frame Pointer)
    IP  = 12,  SP  = 13,  LR  = 14,  PC  = 15
};

// Bare-Metal 32-bit ARM (ARMv7-A) Machine Code Instruction Emitter
class Armv7Encoder {
public:
    Armv7Encoder();
    ~Armv7Encoder();

    void reset();

    void emit32(uint32_t code);

    // ALU Instructions (ARM Condition Code 0xE = AL / Always)
    void add_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void sub_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void mul_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void sdiv_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void and_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void orr_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void eor_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void lsl_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);
    void lsr_reg_reg(Armv7Reg rd, Armv7Reg rn, Armv7Reg rm);

    // Immediate Load (32-bit via MOVW + MOVT sequence)
    void mov_reg_imm32(Armv7Reg rd, uint32_t val);
    void mov_reg_reg(Armv7Reg rd, Armv7Reg rm);

    // Load & Store (base + offset)
    void str_reg_mem(Armv7Reg rt, Armv7Reg rn, uint32_t offset_bytes);
    void ldr_reg_mem(Armv7Reg rt, Armv7Reg rn, uint32_t offset_bytes);

    // AAPCS32 Frame & Function Control Flow
    void push_fp_lr();
    void pop_fp_pc();
    void mov_fp_sp();
    void sub_sp_imm32(uint32_t imm);
    void add_sp_imm32(uint32_t imm);
    void blr(Armv7Reg rn);
    void cmp_reg_reg(Armv7Reg rn, Armv7Reg rm);
    void b_cond(uint8_t cond, int32_t imm24_words);
    void b_uncond(int32_t imm24_words);
    void bx_lr();
    void nop();

    const uint8_t* code_bytes() const { return reinterpret_cast<const uint8_t*>(buffer_); }
    size_t code_size() const { return size_ * sizeof(uint32_t); }
    size_t insn_count() const { return size_; }

private:
    uint32_t* buffer_;
    size_t capacity_;
    size_t size_;
};

class AnastasiaJitRuntime;

class Armv7TargetBackend : public AnaTargetBackend {
public:
    Armv7TargetBackend();
    explicit Armv7TargetBackend(AnastasiaJitRuntime* runtime);
    virtual ~Armv7TargetBackend();

    void set_runtime(AnastasiaJitRuntime* runtime) { runtime_ = runtime; }
    virtual TargetArch arch() const override { return TargetArch::ARMV7; }
    virtual void* compile_function(frontend::Function* fn, frontend::Program* prog) override;
    virtual bool compile_to_elf(frontend::Program* prog, const char* out_filename) override;

private:
    AnastasiaJitRuntime* runtime_;
};

} // namespace backend
} // namespace ana

#endif // ARMV7_BACKEND_H
