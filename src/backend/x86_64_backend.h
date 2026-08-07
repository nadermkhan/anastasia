#ifndef X86_64_BACKEND_H
#define X86_64_BACKEND_H

#include "ana_target_backend.h"
#include "vmem_provider.h"

namespace ana {
namespace backend {

class AnastasiaJitRuntime;

class X86_64TargetBackend : public AnaTargetBackend {
public:
    explicit X86_64TargetBackend(AnastasiaJitRuntime& runtime);
    virtual ~X86_64TargetBackend();

    virtual TargetArch arch() const override { return TargetArch::X86_64; }
    virtual void* compile_function(frontend::Function* fn, frontend::Program* prog) override;
    virtual bool compile_to_elf(frontend::Program* prog, const char* out_filename) override;

private:
    AnastasiaJitRuntime& runtime_;
};

} // namespace backend
} // namespace ana

#endif // X86_64_BACKEND_H
