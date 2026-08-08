#ifndef ANA_VMEM_PROVIDER_H
#define ANA_VMEM_PROVIDER_H

#include "../sys/sys_raw.h"
#include "jit_string_pool.h"

namespace ana {
namespace backend {

class CustomVMemProvider {
public:
    static void* alloc_rw(size_t size) {
        void* ptr = ana::sys::raw_mmap(
            nullptr, size,
            ANA_PROT_READ | ANA_PROT_WRITE,
            ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS,
            -1, 0
        );
        if (ptr == (void*)-1) return nullptr;
        return ptr;
    }

    static bool make_rx(void* ptr, size_t size) {
        return ana::sys::raw_mprotect(ptr, size, ANA_PROT_READ | ANA_PROT_EXEC) == 0;
    }

    static bool make_rw(void* ptr, size_t size) {
        return ana::sys::raw_mprotect(ptr, size, ANA_PROT_READ | ANA_PROT_WRITE) == 0;
    }

    static void free(void* ptr, size_t size) {
        if (ptr) {
            ana::sys::raw_munmap(ptr, size);
        }
    }
};

class AnastasiaJitRuntime {
public:
    AnastasiaJitRuntime() {}

    JitStringPool& string_pool() { return string_pool_; }

private:
    JitStringPool string_pool_;
};

} // namespace backend
} // namespace ana

#endif // ANA_VMEM_PROVIDER_H
