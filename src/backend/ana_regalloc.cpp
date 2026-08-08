#include "ana_regalloc.h"

namespace ana {
namespace backend {

// These must be callee-saved. R8/R9/R10 are caller-saved, so any call the
// function made destroyed the locals living in them.
static const X86Reg kAllocatablePhysRegs[] = {
    X86Reg::RBX,
    X86Reg::R12,
    X86Reg::R13
};
static const size_t kNumAllocatableRegs = sizeof(kAllocatablePhysRegs) / sizeof(kAllocatablePhysRegs[0]);

AnaRegAlloc::AnaRegAlloc()
    : local_count_(0), param_count_(0), saved_reg_count_(0),
      stack_frame_size_(0), requires_frame_(false) {}

X86Reg AnaRegAlloc::saved_reg(uint32_t i) {
    return (i < kNumAllocatableRegs) ? kAllocatablePhysRegs[i] : X86Reg::NONE;
}

AnaRegAlloc::~AnaRegAlloc() {}

void AnaRegAlloc::allocate_registers(frontend::Function* fn) {
    uint32_t max_reg = (fn && fn->local_count > 0) ? fn->local_count - 1 : 0;
    bool has_call = false;
    if (fn) {
        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::CALL_VIRT:
                    case frontend::Opcode::CALL_VIRT_FAST:
                    case frontend::Opcode::NEW_INSTANCE:
                    case frontend::Opcode::SINK_MEM:
                        has_call = true;
                        break;
                    default:
                        break;
                }
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
    if (local_count_ > kMaxLocals) local_count_ = kMaxLocals;

    saved_reg_count_ = (local_count_ < kNumAllocatableRegs)
                       ? local_count_ : static_cast<uint32_t>(kNumAllocatableRegs);

    // Slots 1..saved_reg_count_ (i.e. [rbp-8] downwards) are reserved for the
    // preserved callee-saved registers; spills start below them.
    const size_t reserved = saved_reg_count_;
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
            local_locs_[i].stack_disp = static_cast<int32_t>(8 * (reserved + spill_count));
        }
    }

    uint32_t max_param = 0;
    if (fn) {
        auto check_p = [&](const frontend::Operand& op) {
            if (op.kind == frontend::OperandKind::REGISTER && op.reg.type == frontend::RegisterType::PARAM) {
                if (op.reg.index + 1 > max_param) max_param = op.reg.index + 1;
            }
        };
        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                check_p(insn->dest);
                check_p(insn->src1);
                check_p(insn->src2);
            }
        }
    }

    param_count_ = (fn && fn->params && fn->param_count > max_param) ? fn->param_count : max_param;
    if (param_count_ > 8) param_count_ = 8;

    for (uint32_t i = 0; i < param_count_; ++i) {
        param_locs_[i].kind = RegLocKind::STACK_SPILL;
        param_locs_[i].phys_reg = X86Reg::NONE;
        param_locs_[i].stack_disp = static_cast<int32_t>(8 * (reserved + spill_count + 1 + i));
    }

    size_t total_slots = reserved + spill_count + param_count_;
    // A function that calls out needs a frame even with no locals, otherwise
    // RSP is left 8 mod 16 at the call and violates the System V ABI.
    if (total_slots > 0 || has_call) {
        requires_frame_ = true;
        stack_frame_size_ = (total_slots * 8 + 15) & ~15UL;
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
        case 3: return X86Reg::RCX;
        case 4: return X86Reg::R8;
        case 5: return X86Reg::R9;
        default: return X86Reg::RDI;
    }
}

RegLocation AnaRegAlloc::get_param_loc(uint32_t param_idx) const {
    if (param_idx < param_count_ && param_idx < 8) {
        return param_locs_[param_idx];
    }
    RegLocation loc;
    loc.kind = RegLocKind::STACK_SPILL;
    loc.phys_reg = X86Reg::NONE;
    loc.stack_disp = static_cast<int32_t>(8 * (param_idx + 1));
    return loc;
}

RegLocation AnaRegAlloc::get_local_loc(uint32_t local_idx) const {
    if (local_idx < local_count_ && local_idx < kMaxLocals) {
        return local_locs_[local_idx];
    }
    // Out-of-range register: point at a reserved scratch slot rather than
    // computing a displacement that collides with a live local.
    RegLocation loc;
    loc.kind = RegLocKind::STACK_SPILL;
    loc.phys_reg = X86Reg::NONE;
    loc.stack_disp = static_cast<int32_t>(stack_frame_size_ > 0 ? stack_frame_size_ : 8);
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
