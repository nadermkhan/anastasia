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

// Monomorphic Inline Cache Fallback Miss Handler
extern "C" void* handle_mic_miss(void* obj_ptr, int32_t vtable_slot, uint8_t* patch_addr);

} // namespace backend
} // namespace ana

#endif // ANA_INLINE_CACHE_H
