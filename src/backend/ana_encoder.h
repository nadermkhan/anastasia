#ifndef ANA_ENCODER_H
#define ANA_ENCODER_H

#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

enum class X86Reg : uint8_t {
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8  = 8,
    R9  = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15,
    NONE = 0xFF
};

struct EncoderLabel {
    uint32_t id;
    int32_t offset;
    bool bound;
};

struct LabelReloc {
    uint32_t label_id;
    size_t patch_offset;
    bool is_jcc_rel32;
};

class AnaEncoder {
public:
    AnaEncoder();
    ~AnaEncoder();

    // The encoder owns an mmap'd buffer, so copying would double-unmap it.
    AnaEncoder(const AnaEncoder&) = delete;
    AnaEncoder& operator=(const AnaEncoder&) = delete;

    void reset();

    // True once any emission, label or relocation operation could not be
    // completed. Emitted code must be discarded when this is set.
    bool failed() const { return failed_; }

    // Label management
    uint32_t new_label();
    void bind_label(uint32_t label_id);

    // Raw byte emission
    void emit8(uint8_t byte);
    void emit32(uint32_t dword);
    void emit64(uint64_t qword);
    void emit_bytes(const uint8_t* data, size_t len);

    // REX prefix helper
    void emit_rex(bool w, uint8_t reg_idx, uint8_t index_idx, uint8_t base_idx);
    void emit_modrm(uint8_t mod, uint8_t reg, uint8_t rm);

    // movsxd r64, r/m32 - sign-extends the low 32 bits. Used to give the /32
    // opcode family true 32-bit wrapping semantics.
    void movsxd_reg_reg(X86Reg dst, X86Reg src);
    // mov r32, r/m32 - zero-extends the low 32 bits into the full register.
    void movzxd_reg_reg(X86Reg dst, X86Reg src);
    // cdq - sign-extends EAX into EDX:EAX for 32-bit division.
    void cdq();
    // idiv r/m32 - 32-bit signed division.
    void idiv_reg32(X86Reg src);

    // Core Instruction Encoding
    void mov_reg_reg(X86Reg dst, X86Reg src);
    void mov_reg_imm64(X86Reg dst, uint64_t imm);
    void mov_reg_imm32(X86Reg dst, int32_t imm);
    void mov_reg_mem(X86Reg dst, X86Reg base, int32_t disp);
    void mov_mem_reg(X86Reg base, int32_t disp, X86Reg src);

    void add_reg_reg(X86Reg dst, X86Reg src);
    void add_reg_imm32(X86Reg dst, int32_t imm);
    void add_mem_reg(X86Reg base, int32_t disp, X86Reg src);
    void sub_reg_reg(X86Reg dst, X86Reg src);
    void sub_reg_imm32(X86Reg dst, int32_t imm);
    void imul_reg_reg(X86Reg dst, X86Reg src);
    void imul_reg_imm32(X86Reg dst, X86Reg src, int32_t imm);

    void and_reg_reg(X86Reg dst, X86Reg src);
    void and_reg_imm32(X86Reg dst, int32_t imm);
    void or_reg_reg(X86Reg dst, X86Reg src);
    void or_reg_imm32(X86Reg dst, int32_t imm);
    void xor_reg_reg(X86Reg dst, X86Reg src);
    void xor_reg_imm32(X86Reg dst, int32_t imm);

    void shl_reg_cl(X86Reg dst);
    void shl_reg_imm8(X86Reg dst, uint8_t imm);
    void sar_reg_cl(X86Reg dst);
    void sar_reg_imm8(X86Reg dst, uint8_t imm);
    void shr_reg_cl(X86Reg dst);
    void shr_reg_imm8(X86Reg dst, uint8_t imm);

    void bts_reg_reg(X86Reg dst, X86Reg src);
    void bts_reg_imm8(X86Reg dst, uint8_t imm);
    void btr_reg_reg(X86Reg dst, X86Reg src);
    void btr_reg_imm8(X86Reg dst, uint8_t imm);

    void popcnt_reg_reg(X86Reg dst, X86Reg src);
    void lzcnt_reg_reg(X86Reg dst, X86Reg src);

    void cmp_reg_reg(X86Reg src1, X86Reg src2);
    void cmp_reg_imm32(X86Reg src1, int32_t imm);
    void test_reg_reg(X86Reg src1, X86Reg src2);

    // Control Flow
    void jmp_label(uint32_t label_id);
    void je_label(uint32_t label_id);
    void jne_label(uint32_t label_id);
    void jl_label(uint32_t label_id);
    void jge_label(uint32_t label_id);
    void jz_label(uint32_t label_id);
    void jnz_label(uint32_t label_id);

    void call_reg(X86Reg target);
    void call_rel32_disp(int32_t disp);
    void push_reg(X86Reg reg);
    void pop_reg(X86Reg reg);
    void ret();
    void cqo();
    void idiv_reg(X86Reg reg);

    // Atomics & Memory Barriers
    void lock_cmpxchg_mem_reg(X86Reg base, int32_t disp, X86Reg src);
    void xchg_mem_reg(X86Reg base, int32_t disp, X86Reg src);
    void lock_add_mem_reg(X86Reg base, int32_t disp, X86Reg src);
    void lock_and_mem_reg(X86Reg base, int32_t disp, X86Reg src);
    void lock_or_mem_reg(X86Reg base, int32_t disp, X86Reg src);
    void mfence();
    void clflush(X86Reg base, int32_t disp);

    // SSE2 Scalar & 128-bit Vector
    void addss_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm);
    void addsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm);
    void subsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm);
    void mulsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm);
    void divsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm);
    void movss_xmm_mem(uint8_t dst_xmm, X86Reg base, int32_t disp);
    void movss_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm);
    void movsd_xmm_mem(uint8_t dst_xmm, X86Reg base, int32_t disp);
    void movsd_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm);

    void paddd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm);
    void psubd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm);
    void movdqu_xmm_mem(uint8_t dst_xmm, X86Reg base, int32_t disp);
    void movdqu_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm);

    // VEX (256-bit AVX2) & EVEX (512-bit AVX-512) Encoders
    void emit_vex3(uint8_t m_mmmm, uint8_t pp, bool w, uint8_t vvvv, bool l, uint8_t r, uint8_t x, uint8_t b);
    void emit_evex(uint8_t mm, uint8_t pp, bool w, uint8_t vvvv, uint8_t lll, uint8_t r, uint8_t x, uint8_t b, uint8_t aaa);

    void vpaddd_ymm_ymm(uint8_t dst_ymm, uint8_t src1_ymm, uint8_t src2_ymm);
    void vpaddd_zmm_zmm(uint8_t dst_zmm, uint8_t src1_zmm, uint8_t src2_zmm);
    void vpmulld_ymm_ymm(uint8_t dst_ymm, uint8_t src1_ymm, uint8_t src2_ymm);
    void vmovdqu_ymm_mem(uint8_t dst_ymm, X86Reg base, int32_t disp);
    void vmovdqu_mem_ymm(X86Reg base, int32_t disp, uint8_t src_ymm);
    void vmovdqu_zmm_mem(uint8_t dst_zmm, X86Reg base, int32_t disp);
    void vmovdqu_mem_zmm(X86Reg base, int32_t disp, uint8_t src_zmm);

    // Non-Temporal Data Streaming & Prefetching
    void vmovntdq_ymm_mem(X86Reg base, int32_t disp, uint8_t src_ymm);
    void vmovntdq_zmm_mem(X86Reg base, int32_t disp, uint8_t src_zmm);
    void movntdq_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm);
    void sfence();
    void prefetcht0(X86Reg base, int32_t disp);
    void lea_reg_rip_disp32(X86Reg dst, int32_t disp32);

    // Final resolution
    bool resolve_labels();
    const uint8_t* code_bytes() const { return buffer_; }
    size_t code_size() const { return cursor_; }

private:
    // Returns false (and latches failed_) when the buffer cannot be grown.
    bool ensure_capacity(size_t additional);

    bool failed_;
    uint8_t* buffer_;
    size_t capacity_;
    size_t cursor_;

public:
    static const uint32_t kMaxLabels = 512;
    static const uint32_t kMaxRelocs = 1024;
    static const uint32_t kInvalidLabel = 0xFFFFFFFFu;

private:
    EncoderLabel labels_[kMaxLabels];
    uint32_t label_count_;

    LabelReloc relocs_[kMaxRelocs];
    uint32_t reloc_count_;
};

} // namespace backend
} // namespace ana

#endif // ANA_ENCODER_H
