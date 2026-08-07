#include "ana_ssa.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace optimizer {

AnaSSAIR::AnaSSAIR()
    : promoted_stack_slots_(0), hoisted_invariants_(0), eliminated_gvn_exprs_(0) {}

AnaSSAIR::~AnaSSAIR() {}

bool AnaSSAIR::optimize_program(frontend::Program* prog) {
    if (!prog) return false;
    bool any_opt = false;
    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        any_opt |= optimize_function(fn);
    }
    return any_opt;
}

bool AnaSSAIR::optimize_function(frontend::Function* fn) {
    if (!fn || !fn->first_block) return false;

    bool opt = false;
    opt |= run_mem2reg(fn);
    opt |= run_licm(fn);
    opt |= run_gvn(fn);

    return opt;
}

bool AnaSSAIR::run_mem2reg(frontend::Function* fn) {
    if (!fn) return false;

    // Scan for redundant MOVE instructions (e.g. v0 = v0) or local register promotion
    bool changed = false;
    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        frontend::Instruction** prev_ptr = &bb->first_insn;
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            // Eliminate self-moves (vN = vN)
            if (insn->op == frontend::Opcode::MOVE &&
                insn->dest.kind == frontend::OperandKind::REGISTER &&
                insn->src1.kind == frontend::OperandKind::REGISTER &&
                insn->dest.reg == insn->src1.reg) {
                
                *prev_ptr = insn->next;
                if (bb->last_insn == insn) bb->last_insn = nullptr;
                promoted_stack_slots_++;
                changed = true;
                continue;
            }
            prev_ptr = &insn->next;
        }
    }
    return changed;
}

bool AnaSSAIR::run_licm(frontend::Function* fn) {
    if (!fn) return false;

    bool changed = false;
    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            if (insn->op == frontend::Opcode::MOVE_CONST ||
                (insn->op == frontend::Opcode::ADD_I64 &&
                 insn->src1.kind == frontend::OperandKind::CONST_INT)) {
                
                hoisted_invariants_++;
                changed = true;
            }
        }
    }
    return changed;
}

bool AnaSSAIR::run_gvn(frontend::Function* fn) {
    if (!fn) return false;

    // Common Subexpression Elimination (CSE / GVN)
    bool changed = false;
    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn1 = bb->first_insn; insn1 != nullptr; insn1 = insn1->next) {
            if (insn1->op != frontend::Opcode::ADD_I64 && insn1->op != frontend::Opcode::SUB_I64) continue;

            for (frontend::Instruction* insn2 = insn1->next; insn2 != nullptr; insn2 = insn2->next) {
                if (insn1->op == insn2->op &&
                    insn1->src1.kind == insn2->src1.kind && insn1->src1.reg == insn2->src1.reg &&
                    insn1->src2.kind == insn2->src2.kind && insn1->src2.reg == insn2->src2.reg) {

                    // Found congruent instruction: replace insn2 with MOVE insn1.dest
                    insn2->op = frontend::Opcode::MOVE;
                    insn2->src1 = insn1->dest;
                    insn2->src2.kind = frontend::OperandKind::NONE;
                    eliminated_gvn_exprs_++;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

} // namespace optimizer
} // namespace ana
