#ifndef ANA_TARGET_BACKEND_H
#define ANA_TARGET_BACKEND_H

#include "../frontend/ana_ast.h"
#include <cstdint>
#include <cstddef>

namespace ana {
namespace backend {

enum class TargetArch {
    X86_64,
    AARCH64,
    ARMV7,
    RISCV64,
    XTENSA_LX7
};

class AnaTargetBackend {
public:
    virtual ~AnaTargetBackend() {}

    virtual TargetArch arch() const = 0;
    virtual void* compile_function(frontend::Function* fn, frontend::Program* prog) = 0;
    virtual bool compile_to_elf(frontend::Program* prog, const char* out_filename) = 0;
};

} // namespace backend
} // namespace ana

#endif // ANA_TARGET_BACKEND_H
