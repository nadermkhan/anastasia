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

    // Compiles an Anastasia Program AST into a relocatable ELF .o object file
    bool compile_to_elf(frontend::Program* prog, const char* out_filename);
};

} // namespace backend
} // namespace ana

#endif // ANA_LOWERER_H
