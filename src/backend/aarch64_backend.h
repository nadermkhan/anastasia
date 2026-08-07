#ifndef AARCH64_BACKEND_H
#define AARCH64_BACKEND_H

#include "ana_target_backend.h"
#include "vmem_provider.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

// ARM64 Physical Registers (AAPCS64 ABI)
enum class Arm64Reg : uint8_t {
    X0  = 0,   X1  = 1,   X2  = 2,   X3  = 3,
    X4  = 4,   X5  = 5,   X6  = 6,   X7  = 7,
    X8  = 8,   X9  = 9,   X10 = 10,  X11 = 11,
    X12 = 12,  X13 = 13,  X14 = 14,  X15 = 15,
    X16 = 16,  X17 = 17,  X18 = 18,  X19 = 19,
    X20 = 20,  X21 = 21,  X22 = 22,  X23 = 23,
    X24 = 24,  X25 = 25,  X26 = 26,  X27 = 27,
    X28 = 28,  FP  = 29,  LR  = 30,  SP  = 31,
    XZR = 31
};

// Bare-Metal 32-bit Fixed Width ARM64 Instruction Emitter
class AArch64Encoder {
public:
    AArch64Encoder();
    ~AArch64Encoder();

    void reset();

    void emit32(uint32_t code);

    // Basic ALU Encodings
    void add_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void sub_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void mul_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void sdiv_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void and_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void orr_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void eor_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void lsl_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);
    void lsr_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm);

    // Immediate Move (64-bit wide via MOVZ + MOVK sequence)
    void mov_reg_imm64(Arm64Reg rd, uint64_t val);
    void mov_reg_reg(Arm64Reg rd, Arm64Reg rm);

    // Load & Store (base + offset)
    void str_reg_mem(Arm64Reg rt, Arm64Reg rn, uint32_t offset_bytes);
    void ldr_reg_mem(Arm64Reg rt, Arm64Reg rn, uint32_t offset_bytes);

    // AAPCS64 Frame & Function Control Flow
    void push_fp_lr();
    void pop_fp_lr();
    void mov_fp_sp();
    void sub_sp_imm32(uint32_t imm);
    void add_sp_imm32(uint32_t imm);
    void ret();
    void nop();

    const uint8_t* code_bytes() const { return reinterpret_cast<const uint8_t*>(buffer_); }
    size_t code_size() const { return size_ * sizeof(uint32_t); }
    size_t insn_count() const { return size_; }

private:
    uint32_t* buffer_;
    size_t capacity_;
    size_t size_;
};

class AArch64TargetBackend : public AnaTargetBackend {
public:
    AArch64TargetBackend();
    virtual ~AArch64TargetBackend();

    virtual TargetArch arch() const override { return TargetArch::AARCH64; }
    virtual void* compile_function(frontend::Function* fn, frontend::Program* prog) override;
    virtual bool compile_to_elf(frontend::Program* prog, const char* out_filename) override;
};

} // namespace backend
} // namespace ana

#endif // AARCH64_BACKEND_H
