#include "x86_64_backend.h"
#include "ana_lowerer.h"

namespace ana {
namespace backend {

X86_64TargetBackend::X86_64TargetBackend(AnastasiaJitRuntime& runtime)
    : runtime_(runtime) {}

X86_64TargetBackend::~X86_64TargetBackend() {}

void* X86_64TargetBackend::compile_function(frontend::Function* fn, frontend::Program* prog) {
    AnaLowerer lowerer(runtime_);
    return lowerer.compile_function(fn, prog);
}

bool X86_64TargetBackend::compile_to_elf(frontend::Program* prog, const char* out_filename) {
    AnaLowerer lowerer(runtime_);
    return lowerer.compile_to_elf(prog, out_filename);
}

} // namespace backend
} // namespace ana
