#include "tlab_provider.h"
#include "object_heap.h"
#include <signal.h>

namespace ana {
namespace sys {

static TLAB g_main_tlab = { nullptr, nullptr, nullptr };
static bool g_tlab_initialized = false;

TLAB* get_thread_tlab() {
    if (!g_main_tlab.slab_start) {
        // Allocate initial TLAB slab with a PROT_NONE guard page at the boundary
        size_t alloc_size = TLAB_SLAB_SIZE + 4096; // 64 KB slab + 4 KB guard page
        uint8_t* mem = static_cast<uint8_t*>(raw_mmap(nullptr, alloc_size, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0));
        if (mem && mem != (uint8_t*)-1) {
            g_main_tlab.slab_start = mem;
            g_main_tlab.top = mem;
            g_main_tlab.end = mem + TLAB_SLAB_SIZE;
            // Mark guard page at end boundary as PROT_NONE
            raw_mprotect(g_main_tlab.end, 4096, ANA_PROT_NONE);
        }
    }
    return &g_main_tlab;
}

#if defined(__linux__) && defined(__x86_64__)
struct k_sigaction {
    void (*handler)(int, siginfo_t*, void*);
    unsigned long flags;
    void (*restorer)(void);
    unsigned long mask;
};

static void sigsegv_tlab_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig; (void)info; (void)ucontext;
    TLAB* tlab = get_thread_tlab();
    
    // Allocate a new TLAB slab
    size_t alloc_size = TLAB_SLAB_SIZE + 4096;
    uint8_t* mem = static_cast<uint8_t*>(raw_mmap(nullptr, alloc_size, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0));
    if (mem && mem != (uint8_t*)-1) {
        tlab->slab_start = mem;
        tlab->top = mem;
        tlab->end = mem + TLAB_SLAB_SIZE;
        raw_mprotect(tlab->end, 4096, ANA_PROT_NONE);
    }
}
#endif

void init_tlab_subsystem() {
    if (g_tlab_initialized) return;
    g_tlab_initialized = true;

#if defined(__linux__) && defined(__x86_64__)
    k_sigaction sa;
    freestanding_memset(&sa, 0, sizeof(sa));
    sa.handler = sigsegv_tlab_handler;
    sa.flags = SA_SIGINFO | SA_NODEFER;

    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(13), "D"(SIGSEGV), "S"(&sa), "d"((void*)0), "r"(8UL)
        : "rcx", "r11", "memory"
    );
#endif
    get_thread_tlab();
}

void* tlab_allocate(uint32_t instance_size, void* vtable_ptr, uint32_t class_id) {
    TLAB* tlab = get_thread_tlab();
    uint32_t total_size = (sizeof(ObjectHeader) + instance_size + 7) & ~7U;

    // Fast-path branchless allocation simulation / check
    if (tlab->top + total_size >= tlab->end) {
        // Trigger allocation refill
        size_t alloc_size = TLAB_SLAB_SIZE + 4096;
        uint8_t* mem = static_cast<uint8_t*>(raw_mmap(nullptr, alloc_size, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0));
        if (mem && mem != (uint8_t*)-1) {
            tlab->slab_start = mem;
            tlab->top = mem;
            tlab->end = mem + TLAB_SLAB_SIZE;
            raw_mprotect(tlab->end, 4096, ANA_PROT_NONE);
        }
    }

    ObjectHeader* obj = reinterpret_cast<ObjectHeader*>(tlab->top);
    tlab->top += total_size;

    obj->vtable_ptr = vtable_ptr;
    obj->class_id = class_id;
    obj->instance_size = instance_size;

    return static_cast<void*>(obj);
}

} // namespace sys
} // namespace ana
