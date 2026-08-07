#ifndef ANA_TLAB_PROVIDER_H
#define ANA_TLAB_PROVIDER_H

#include "sys_raw.h"

namespace ana {
namespace sys {

constexpr size_t TLAB_SLAB_SIZE = 65536; // 64 KB per TLAB slab

struct TLAB {
    uint8_t* top;
    uint8_t* end;
    uint8_t* slab_start;
};

// Global freestanding TLAB access
TLAB* get_thread_tlab();

// Initialize TLAB subsystem and install SIGSEGV signal handler for guard pages & Remset
void init_tlab_subsystem();

// Fast-path TLAB allocation helper
void* tlab_allocate(uint32_t instance_size, void* vtable_ptr, uint32_t class_id);

} // namespace sys
} // namespace ana

#endif // ANA_TLAB_PROVIDER_H
