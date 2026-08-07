#ifndef ANA_SSA_H
#define ANA_SSA_H

#include "../frontend/ana_ast.h"

namespace ana {
namespace optimizer {

class AnaSSAIR {
public:
    AnaSSAIR();
    ~AnaSSAIR();

    // Runs SSA optimization pipeline (mem2reg, LICM, GVN) on program AST
    bool optimize_program(frontend::Program* prog);

    // Runs SSA optimization pipeline on a single function AST
    bool optimize_function(frontend::Function* fn);

    uint32_t promoted_stack_slots() const { return promoted_stack_slots_; }
    uint32_t hoisted_invariants() const { return hoisted_invariants_; }
    uint32_t eliminated_gvn_exprs() const { return eliminated_gvn_exprs_; }

private:
    bool run_mem2reg(frontend::Function* fn);
    bool run_licm(frontend::Function* fn);
    bool run_gvn(frontend::Function* fn);

    uint32_t promoted_stack_slots_;
    uint32_t hoisted_invariants_;
    uint32_t eliminated_gvn_exprs_;
};

} // namespace optimizer
} // namespace ana

#endif // ANA_SSA_H
