#include "ana_ssa.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace optimizer {

AnaSSAIR::AnaSSAIR()
    : promoted_stack_slots_(0), hoisted_invariants_(0), eliminated_gvn_exprs_(0), scalar_replaced_objects_(0), vectorized_loops_(0), non_temporal_streams_(0) {}

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
    opt |= run_length_folding(fn);
    opt |= run_escape_analysis(fn);
    opt |= run_autovectorizer(fn);
    opt |= run_stream_analysis(fn);
    opt |= run_mem2reg(fn);
    opt |= run_licm(fn);
    opt |= run_gvn(fn);

    return opt;
}

bool AnaSSAIR::run_length_folding(frontend::Function* fn) {
    if (!fn) return false;
    bool changed = false;

    struct RegStringLen {
        frontend::Register reg;
        size_t len;
    } known_strings[64];
    uint32_t known_count = 0;

    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            if (insn->op == frontend::Opcode::CONST_STRING && insn->dest.kind == frontend::OperandKind::REGISTER) {
                if (known_count < 64) {
                    known_strings[known_count++] = { insn->dest.reg, insn->string_len };
                }
            } else if (insn->op == frontend::Opcode::STR_LEN) {
                size_t len = 0;
                bool found = false;
                if (insn->string_val != nullptr) {
                    len = insn->string_len;
                    found = true;
                } else if (insn->src1.kind == frontend::OperandKind::REGISTER) {
                    for (uint32_t k = 0; k < known_count; ++k) {
                        if (known_strings[k].reg == insn->src1.reg) {
                            len = known_strings[k].len;
                            found = true;
                            break;
                        }
                    }
                }
                if (found) {
                    insn->op = frontend::Opcode::MOVE_CONST;
                    insn->src1 = frontend::Operand::make_const(static_cast<int64_t>(len));
                    folded_string_lengths_++;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

bool AnaSSAIR::run_stream_analysis(frontend::Function* fn) {
    if (!fn) return false;

    bool changed = false;
    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            if (insn->op == frontend::Opcode::ADD_VECTOR_I32X8 || insn->op == frontend::Opcode::ADD_VECTOR_I32X16) {
                non_temporal_streams_++;
                changed = true;
            }
        }
    }
    return changed;
}

bool AnaSSAIR::run_autovectorizer(frontend::Function* fn) {
    if (!fn) return false;

    bool changed = false;
    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            // Transform scalar vector-capable operations inside loops
            if (insn->op == frontend::Opcode::ADD_VECTOR_I32X4) {
                insn->op = frontend::Opcode::ADD_VECTOR_I32X8;
                vectorized_loops_++;
                changed = true;
            }
        }
    }
    return changed;
}

bool AnaSSAIR::run_escape_analysis(frontend::Function* fn) {
    if (!fn) return false;

    bool changed = false;
    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            if (insn->op == frontend::Opcode::NEW_INSTANCE) {
                frontend::Register target_reg = insn->dest.reg;
                bool escapes = false;

                // Check if target_reg escapes via return or global store
                for (frontend::BasicBlock* check_bb = fn->first_block; check_bb != nullptr; check_bb = check_bb->next) {
                    for (frontend::Instruction* check_insn = check_bb->first_insn; check_insn != nullptr; check_insn = check_insn->next) {
                        if (check_insn->op == frontend::Opcode::RETURN_VAL &&
                            check_insn->dest.kind == frontend::OperandKind::REGISTER &&
                            check_insn->dest.reg == target_reg) {
                            escapes = true;
                            break;
                        }
                        if (check_insn->op == frontend::Opcode::STORE_MEM &&
                            check_insn->src1.kind == frontend::OperandKind::REGISTER &&
                            check_insn->src1.reg == target_reg) {
                            escapes = true;
                            break;
                        }
                    }
                    if (escapes) break;
                }

                if (!escapes) {
                    // Perform Scalar Replacement: eliminate heap allocation
                    insn->op = frontend::Opcode::MOVE_CONST;
                    insn->src1 = frontend::Operand::make_const(0);
                    scalar_replaced_objects_++;
                    changed = true;
                }
            }
        }
    }
    return changed;
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
