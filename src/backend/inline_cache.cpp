#include "inline_cache.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

static uint32_t g_mic_misses = 0;

uint32_t get_mic_miss_count() {
    return g_mic_misses;
}

void reset_mic_miss_count() {
    g_mic_misses = 0;
}

extern "C" void* handle_mic_miss(void* obj_ptr, int32_t vtable_slot, uint8_t* patch_addr) {
    g_mic_misses++;

    if (!obj_ptr || !patch_addr) return nullptr;

    // Dereference object pointer to get real VTable pointer
    void** vtable_ptr = *reinterpret_cast<void***>(obj_ptr);
    if (!vtable_ptr) return nullptr;

    void* target_fn = vtable_ptr[vtable_slot];

    // Backpatching: flip W^X permissions to RW
    uintptr_t page_align_mask = ~(static_cast<uintptr_t>(4095));
    void* page_addr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(patch_addr) & page_align_mask);

    if (ana::sys::raw_mprotect(page_addr, 4096, ANA_PROT_READ | ANA_PROT_WRITE) == 0) {
        // Atomic 64-bit store of VTable pointer
        __atomic_store_n(reinterpret_cast<void**>(patch_addr), vtable_ptr, __ATOMIC_RELEASE);

        // Execute CLFLUSH to invalidate CPU instruction prefetch cache line
        __asm__ __volatile__(
            "clflush (%0)\n\t"
            "mfence"
            :
            : "r"(patch_addr)
            : "memory"
        );

        // Flush CPU instruction cache
        ana::sys::clear_icache(patch_addr, sizeof(void*));

        // Restore permissions
        ana::sys::raw_mprotect(page_addr, 4096, ANA_PROT_READ | ANA_PROT_WRITE | ANA_PROT_EXEC);
    }

    return target_fn;
}

} // namespace backend
} // namespace ana
