#ifndef ANA_LOWERER_H
#define ANA_LOWERER_H

#include "../frontend/ana_ast.h"
#include "vmem_provider.h"
#include "ana_encoder.h"
#include "ana_regalloc.h"

namespace ana {
namespace backend {

class AnaLowerer {
private:
    AnastasiaJitRuntime& runtime_;

public:
    explicit AnaLowerer(AnastasiaJitRuntime& runtime);

    // Compiles an Anastasia function AST into a native machine code function pointer
    void* compile_function(frontend::Function* fn, frontend::Program* prog = nullptr);
};

} // namespace backend
} // namespace ana

#endif // ANA_LOWERER_H
