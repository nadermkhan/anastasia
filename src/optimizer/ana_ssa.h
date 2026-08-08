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
    uint32_t scalar_replaced_objects() const { return scalar_replaced_objects_; }
    uint32_t vectorized_loops() const { return vectorized_loops_; }
    uint32_t non_temporal_streams() const { return non_temporal_streams_; }
    uint32_t folded_string_lengths() const { return folded_string_lengths_; }

private:
    bool run_length_folding(frontend::Function* fn);
    bool run_escape_analysis(frontend::Function* fn);
    bool run_autovectorizer(frontend::Function* fn);
    bool run_stream_analysis(frontend::Function* fn);
    bool run_mem2reg(frontend::Function* fn);
    bool run_licm(frontend::Function* fn);
    bool run_gvn(frontend::Function* fn);

    uint32_t promoted_stack_slots_;
    uint32_t hoisted_invariants_;
    uint32_t eliminated_gvn_exprs_;
    uint32_t scalar_replaced_objects_;
    uint32_t vectorized_loops_;
    uint32_t non_temporal_streams_;
    uint32_t folded_string_lengths_{0};
};

} // namespace optimizer
} // namespace ana

#endif // ANA_SSA_H
