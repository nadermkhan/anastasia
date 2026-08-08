#ifndef ANA_INLINE_CACHE_H
#define ANA_INLINE_CACHE_H

#include "../sys/sys_raw.h"
#include "vmem_provider.h"
#include <stdint.h>

namespace ana {
namespace backend {

struct InlineCacheSite {
    uint8_t* patch_addr;      // Memory location of the expected VTable pointer
    int32_t  vtable_slot;     // VTable slot index
    uint32_t miss_count;      // Monomorphic inline cache miss counter
};

// JIT code region registry.
//
// The inline-cache backpatcher must only change page protections on memory the
// JIT itself mapped. Flipping protections on a caller-owned page (a stack slot,
// for example) strips write access from that page and faults the process on the
// very next store.
void register_jit_code_region(void* base, size_t size);
bool is_jit_code_address(const void* addr);

// Monomorphic Inline Cache Fallback Miss Handler
extern "C" void* handle_mic_miss(void* obj_ptr, int32_t vtable_slot, uint8_t* patch_addr);

} // namespace backend
} // namespace ana

#endif // ANA_INLINE_CACHE_H
