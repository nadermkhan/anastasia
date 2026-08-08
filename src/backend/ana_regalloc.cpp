#include "ana_regalloc.h"

namespace ana {
namespace backend {

static const X86Reg kAllocatablePhysRegs[] = {
    X86Reg::R8,
    X86Reg::R9,
    X86Reg::R10
};
static const size_t kNumAllocatableRegs = sizeof(kAllocatablePhysRegs) / sizeof(kAllocatablePhysRegs[0]);

AnaRegAlloc::AnaRegAlloc()
    : local_count_(0), param_count_(0), stack_frame_size_(0), requires_frame_(false) {}

AnaRegAlloc::~AnaRegAlloc() {}

void AnaRegAlloc::allocate_registers(frontend::Function* fn) {
    uint32_t max_reg = (fn && fn->local_count > 0) ? fn->local_count - 1 : 0;
    if (fn) {
        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                if (insn->dest.kind == frontend::OperandKind::REGISTER && insn->dest.reg.type == frontend::RegisterType::LOCAL) {
                    if (insn->dest.reg.index > max_reg) max_reg = insn->dest.reg.index;
                }
                if (insn->src1.kind == frontend::OperandKind::REGISTER && insn->src1.reg.type == frontend::RegisterType::LOCAL) {
                    if (insn->src1.reg.index > max_reg) max_reg = insn->src1.reg.index;
                }
                if (insn->src2.kind == frontend::OperandKind::REGISTER && insn->src2.reg.type == frontend::RegisterType::LOCAL) {
                    if (insn->src2.reg.index > max_reg) max_reg = insn->src2.reg.index;
                }
            }
        }
    }

    local_count_ = max_reg + 1;
    if (local_count_ > 64) local_count_ = 64;

    size_t spill_count = 0;

    for (uint32_t i = 0; i < local_count_; ++i) {
        if (i < kNumAllocatableRegs) {
            local_locs_[i].kind = RegLocKind::PHYSICAL_REG;
            local_locs_[i].phys_reg = kAllocatablePhysRegs[i];
            local_locs_[i].stack_disp = 0;
        } else {
            spill_count++;
            local_locs_[i].kind = RegLocKind::STACK_SPILL;
            local_locs_[i].phys_reg = X86Reg::NONE;
            local_locs_[i].stack_disp = static_cast<int32_t>(8 * spill_count);
        }
    }

    param_count_ = (fn && fn->params) ? fn->param_count : 0;

    if (spill_count > 0) {
        requires_frame_ = true;
        stack_frame_size_ = (spill_count * 8 + 15) & ~15UL;
    } else {
        requires_frame_ = false;
        stack_frame_size_ = 0;
    }
}

X86Reg AnaRegAlloc::get_param_raw_reg(uint32_t param_idx) const {
    switch (param_idx) {
        case 0: return X86Reg::RDI;
        case 1: return X86Reg::RSI;
        case 2: return X86Reg::RDX;
        case 3: return X86Reg::R8;
        case 4: return X86Reg::R9;
        case 5: return X86Reg::R10;
        default: return X86Reg::RDI;
    }
}

RegLocation AnaRegAlloc::get_param_loc(uint32_t param_idx) const {
    RegLocation loc;
    loc.kind = RegLocKind::PHYSICAL_REG;
    loc.phys_reg = get_param_raw_reg(param_idx);
    loc.stack_disp = 0;
    return loc;
}

RegLocation AnaRegAlloc::get_local_loc(uint32_t local_idx) const {
    if (local_idx < local_count_ && local_idx < 64) {
        return local_locs_[local_idx];
    }
    RegLocation loc;
    loc.kind = RegLocKind::STACK_SPILL;
    loc.phys_reg = X86Reg::NONE;
    size_t idx = (local_idx >= kNumAllocatableRegs) ? (local_idx - kNumAllocatableRegs + 1) : 1;
    loc.stack_disp = static_cast<int32_t>(8 * idx);
    return loc;
}

RegLocation AnaRegAlloc::get_reg_loc(frontend::Register reg) const {
    if (reg.type == frontend::RegisterType::PARAM) {
        return get_param_loc(reg.index);
    } else {
        return get_local_loc(reg.index);
    }
}

} // namespace backend
} // namespace ana
