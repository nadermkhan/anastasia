#include "inline_cache.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

static uint32_t g_mic_misses = 0;

namespace {
struct JitRegion { uintptr_t start; uintptr_t end; };
constexpr uint32_t kMaxJitRegions = 512;
JitRegion g_jit_regions[kMaxJitRegions];
uint32_t g_jit_region_count = 0;
} // namespace

void register_jit_code_region(void* base, size_t size) {
    if (!base || size == 0) return;
    uint32_t idx = __atomic_fetch_add(&g_jit_region_count, 1u, __ATOMIC_ACQ_REL);
    if (idx >= kMaxJitRegions) {
        // Saturate rather than scribbling past the table.
        __atomic_store_n(&g_jit_region_count, kMaxJitRegions, __ATOMIC_RELEASE);
        return;
    }
    g_jit_regions[idx].start = reinterpret_cast<uintptr_t>(base);
    g_jit_regions[idx].end = reinterpret_cast<uintptr_t>(base) + size;
}

bool is_jit_code_address(const void* addr) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    uint32_t n = __atomic_load_n(&g_jit_region_count, __ATOMIC_ACQUIRE);
    if (n > kMaxJitRegions) n = kMaxJitRegions;
    for (uint32_t i = 0; i < n; ++i) {
        if (a >= g_jit_regions[i].start && a < g_jit_regions[i].end) return true;
    }
    return false;
}

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
    // Reject out-of-range slots instead of reading arbitrary memory.
    if (vtable_slot < 0 || vtable_slot >= 16) return nullptr;

    void* target_fn = vtable_ptr[vtable_slot];

    // Backpatching: flip W^X permissions to RW
    uintptr_t page_align_mask = ~(static_cast<uintptr_t>(4095));
    void* page_addr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(patch_addr) & page_align_mask);

    if (!is_jit_code_address(patch_addr)) {
        // The patch site is not JIT-owned code -- it is an ordinary writable
        // data or stack slot. Publish the pointer directly. Re-protecting a
        // page we do not own would revoke write access from the caller's own
        // memory (previously this turned the caller's stack read-only and
        // killed the process on the next store).
        __atomic_store_n(reinterpret_cast<void**>(patch_addr), vtable_ptr, __ATOMIC_RELEASE);
        return target_fn;
    }

    if (ana::sys::raw_mprotect(page_addr, 4096, ANA_PROT_READ | ANA_PROT_WRITE) == 0) {
        // Atomic 64-bit store of VTable pointer
        __atomic_store_n(reinterpret_cast<void**>(patch_addr), vtable_ptr, __ATOMIC_RELEASE);

        // Flush CPU instruction cache
        ana::sys::clear_icache(patch_addr, sizeof(void*));

        // Restore permissions. Re-arming as RWX would leave the page
        // permanently writable+executable and defeat W^X, so go back to R+X.
        ana::sys::raw_mprotect(page_addr, 4096, ANA_PROT_READ | ANA_PROT_EXEC);
    }

    return target_fn;
}

} // namespace backend
} // namespace ana
