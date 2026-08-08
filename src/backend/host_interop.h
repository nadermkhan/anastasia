#ifndef ANA_HOST_INTEROP_H
#define ANA_HOST_INTEROP_H

#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

struct HostFuncEntry {
    const char* name;
    void* host_fn_ptr;
    void* JIT_trampoline_ptr;
    bool requires_unboxing;
};

class HostInterop {
public:
    static HostInterop& instance();

    HostInterop();
    ~HostInterop();

    void* register_host_function(const char* name, void* fn_ptr, bool requires_unboxing = false);
    void* get_trampoline(const char* name);

    uint32_t total_registered_functions() const { return func_count_; }

private:
    HostFuncEntry funcs_[128];
    uint32_t func_count_;
};

extern "C" void* ana_register_host_func(const char* name, void* fn_ptr);
extern "C" void  ana_benchmark_consume(int64_t val);

} // namespace backend
} // namespace ana

#endif // ANA_HOST_INTEROP_H
