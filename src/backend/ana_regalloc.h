#ifndef ANA_REGALLOC_H
#define ANA_REGALLOC_H

#include "ana_encoder.h"
#include "../frontend/ana_ast.h"

namespace ana {
namespace backend {

enum class RegLocKind : uint8_t {
    PHYSICAL_REG,
    STACK_SPILL
};

struct RegLocation {
    RegLocKind kind;
    X86Reg phys_reg;
    int32_t stack_disp; // Offset from RBP: [rbp - disp]
};

class AnaRegAlloc {
public:
    AnaRegAlloc();
    ~AnaRegAlloc();

    void allocate_registers(frontend::Function* fn);

    RegLocation get_reg_loc(frontend::Register reg) const;
    RegLocation get_local_loc(uint32_t local_idx) const;
    RegLocation get_param_loc(uint32_t param_idx) const;
    X86Reg get_param_raw_reg(uint32_t param_idx) const;

    size_t stack_frame_size() const { return stack_frame_size_; }
    bool requires_frame() const { return requires_frame_; }
    uint32_t param_count() const { return param_count_; }
    uint32_t local_count() const { return local_count_; }

    // Locals are held in callee-saved registers, so the prologue must preserve
    // them and the epilogue must restore them.
    uint32_t saved_reg_count() const { return saved_reg_count_; }
    static X86Reg saved_reg(uint32_t i);
    static int32_t saved_reg_disp(uint32_t i) { return static_cast<int32_t>(8 * (i + 1)); }

    // Documented register file is v0..v999.
    static const uint32_t kMaxLocals = 1024;

private:
    RegLocation local_locs_[kMaxLocals];
    RegLocation param_locs_[8];
    uint32_t local_count_;
    uint32_t param_count_;
    uint32_t saved_reg_count_;

    size_t stack_frame_size_;
    bool requires_frame_;
};

} // namespace backend
} // namespace ana

#endif // ANA_REGALLOC_H
