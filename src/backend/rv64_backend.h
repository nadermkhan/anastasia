#ifndef RV64_BACKEND_H
#define RV64_BACKEND_H

#include "ana_target_backend.h"
#include "vmem_provider.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

// RISC-V 64-bit Physical Registers (RV64G ABI)
enum class Rv64Reg : uint8_t {
    ZERO = 0,   RA   = 1,   SP   = 2,   GP   = 3,
    TP   = 4,   T0   = 5,   T1   = 6,   T2   = 7,
    FP   = 8,   S1   = 9,   A0   = 10,  A1   = 11,
    A2   = 12,  A3   = 13,  A4   = 14,  A5   = 15,
    A6   = 16,  A7   = 17,  S2   = 18,  S3   = 19,
    S4   = 20,  S5   = 21,  S6   = 22,  S7   = 23,
    S8   = 24,  S9   = 25,  S10  = 26,  S11  = 27,
    T3   = 28,  T4   = 29,  T5   = 30,  T6   = 31
};

// Bare-Metal 32-bit Fixed Width RISC-V (RV64GC) Instruction Emitter
class Rv64Encoder {
public:
    Rv64Encoder();
    ~Rv64Encoder();

    void reset();

    void emit32(uint32_t code);

    // R-type Encodings (op = 0x33)
    void add_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void sub_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void mul_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void div_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void and_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void or_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void xor_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void sll_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);
    void srl_reg_reg(Rv64Reg rd, Rv64Reg rs1, Rv64Reg rs2);

    // I-type Encodings
    void addi_reg_imm(Rv64Reg rd, Rv64Reg rs1, int32_t imm12);
    void ld_reg_mem(Rv64Reg rd, Rv64Reg rs1, int32_t offset12);
    void jalr_reg(Rv64Reg rd, Rv64Reg rs1, int32_t offset12);

    // S-type Encodings
    void sd_reg_mem(Rv64Reg rs2, Rv64Reg rs1, int32_t offset12);

    // U-type Encodings
    void lui_reg_imm20(Rv64Reg rd, int32_t imm20);

    // B-type Encodings
    void beq_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset);
    void bne_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset);
    void blt_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset);
    void bge_reg_reg(Rv64Reg rs1, Rv64Reg rs2, int32_t offset);

    // J-type Encodings
    void jal_reg(Rv64Reg rd, int32_t offset);

    // Composite Encodings
    void mov_reg_imm64(Rv64Reg rd, uint64_t val);
    void mov_reg_reg(Rv64Reg rd, Rv64Reg rs1);
    void push_fp_ra();
    void pop_fp_ra();
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

class AnastasiaJitRuntime;

class Rv64TargetBackend : public AnaTargetBackend {
public:
    Rv64TargetBackend();
    explicit Rv64TargetBackend(AnastasiaJitRuntime* runtime);
    virtual ~Rv64TargetBackend();

    void set_runtime(AnastasiaJitRuntime* runtime) { runtime_ = runtime; }
    virtual TargetArch arch() const override { return TargetArch::RISCV64; }
    virtual void* compile_function(frontend::Function* fn, frontend::Program* prog) override;
    virtual bool compile_to_elf(frontend::Program* prog, const char* out_filename) override;

private:
    AnastasiaJitRuntime* runtime_;
};

} // namespace backend
} // namespace ana

#endif // RV64_BACKEND_H
