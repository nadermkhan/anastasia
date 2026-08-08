#include "sys_raw.h"
#include "cpu_features.h"

namespace ana {
namespace sys {

void* raw_mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset) {
#if defined(__linux__) && defined(__x86_64__)
    if ((prot & ANA_PROT_WRITE) && (prot & ANA_PROT_EXEC)) {
        prot &= ~ANA_PROT_EXEC;
    }

    int64_t ret;
    int64_t flg = static_cast<int64_t>(flags);
    int64_t f   = static_cast<int64_t>(fd);
    int64_t off = static_cast<int64_t>(offset);

    // The inputs were "r"-constrained while the asm clobbered r10/r8/r9,
    // so the compiler was free to place an input in a register the block
    // then destroyed. Bind them to their ABI registers explicitly.
    register int64_t r10 __asm__("r10") = flg;
    register int64_t r8  __asm__("r8")  = f;
    register int64_t r9  __asm__("r9")  = off;

    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(9), "D"(addr), "S"(length), "d"(prot), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );

    if (static_cast<uintptr_t>(ret) >= static_cast<uintptr_t>(-4095UL)) {
        return reinterpret_cast<void*>(-1);
    }
    return reinterpret_cast<void*>(ret);
#elif defined(_WIN32) || defined(_WIN64)
    (void)flags; (void)fd; (void)offset;
    // Win32 VirtualAlloc allocation
    extern "C" __declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, uint32_t, uint32_t);
    uint32_t win_prot = 0x04; // PAGE_READWRITE
    if (prot & ANA_PROT_EXEC) win_prot = 0x40; // PAGE_EXECUTE_READWRITE
    void* ptr = VirtualAlloc(addr, length, 0x1000 | 0x2000 /* MEM_COMMIT | MEM_RESERVE */, win_prot);
    return ptr ? ptr : reinterpret_cast<void*>(-1);
#else
    (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    return reinterpret_cast<void*>(-1);
#endif
}

int raw_mprotect(void* addr, size_t length, int prot) {
#if defined(__linux__) && defined(__x86_64__)
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t page_size = 4096;
    uintptr_t aligned_start = start & ~(page_size - 1);
    size_t aligned_len = length + (start - aligned_start);
    // The length was never rounded up to a whole page, so a range that
    // straddled a page boundary left its final page at the old protection.
    aligned_len = (aligned_len + (page_size - 1)) & ~(page_size - 1);

    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(10), "D"(aligned_start), "S"(aligned_len), "d"(prot)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#elif defined(_WIN32) || defined(_WIN64)
    extern "C" __declspec(dllimport) int __stdcall VirtualProtect(void*, size_t, uint32_t, uint32_t*);
    uint32_t win_prot = 0x04; // PAGE_READWRITE
    if ((prot & ANA_PROT_READ) && (prot & ANA_PROT_EXEC)) win_prot = 0x20; // PAGE_EXECUTE_READ
    uint32_t old_prot = 0;
    return VirtualProtect(addr, length, win_prot, &old_prot) ? 0 : -1;
#else
    (void)addr; (void)length; (void)prot;
    return -1;
#endif
}

int raw_munmap(void* addr, size_t length) {
#if defined(__linux__) && defined(__x86_64__)
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t page_size = 4096;
    uintptr_t aligned_start = start & ~(page_size - 1);
    size_t aligned_len = length + (start - aligned_start);
    aligned_len = (aligned_len + (page_size - 1)) & ~(page_size - 1);

    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(11), "D"(aligned_start), "S"(aligned_len)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#elif defined(_WIN32) || defined(_WIN64)
    (void)length;
    extern "C" __declspec(dllimport) int __stdcall VirtualFree(void*, size_t, uint32_t);
    return VirtualFree(addr, 0, 0x8000 /* MEM_RELEASE */) ? 0 : -1;
#else
    (void)addr; (void)length;
    return -1;
#endif
}

int64_t raw_write(int fd, const void* buf, size_t count) {
#if defined(__linux__) && defined(__x86_64__)
    // write(2) may transfer fewer bytes than requested and may fail with
    // EINTR. The old one-shot call silently dropped the tail of long
    // diagnostics and of every large payload written to a pipe.
    const unsigned char* p = static_cast<const unsigned char*>(buf);
    int64_t fd64 = static_cast<int64_t>(fd);
    size_t done = 0;
    while (done < count) {
        int64_t ret;
        const void* cur = static_cast<const void*>(p + done);
        size_t remaining = count - done;
        __asm__ __volatile__(
            "syscall"
            : "=a"(ret)
            : "a"(1), "D"(fd64), "S"(cur), "d"(remaining)
            : "rcx", "r11", "memory"
        );
        if (ret < 0) {
            if (ret == -4) continue; // EINTR
            return done ? static_cast<int64_t>(done) : ret;
        }
        if (ret == 0) break;
        done += static_cast<size_t>(ret);
    }
    return static_cast<int64_t>(done);
#elif defined(_WIN32) || defined(_WIN64)
    extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(uint32_t);
    extern "C" __declspec(dllimport) int   __stdcall WriteFile(void*, const void*, uint32_t, uint32_t*, void*);
    void* h_out = GetStdHandle(static_cast<uint32_t>(-11) /* STD_OUTPUT_HANDLE */);
    uint32_t written = 0;
    if (WriteFile(h_out, buf, static_cast<uint32_t>(count), &written, nullptr)) {
        return written;
    }
    return -1;
#else
    (void)fd; (void)buf; (void)count;
    return -1;
#endif
}

int64_t raw_read(int fd, void* buf, size_t count) {
#if defined(__linux__) && defined(__x86_64__)
    // Retry on EINTR. Short reads are still reported to the caller, which is
    // POSIX behaviour; callers that need a whole file must loop.
    int64_t ret;
    for (;;) {
        __asm__ __volatile__(
            "syscall"
            : "=a"(ret)
            : "a"(0), "D"(static_cast<int64_t>(fd)), "S"(buf), "d"(count)
            : "rcx", "r11", "memory"
        );
        if (ret == -4) continue; // EINTR
        break;
    }
    return ret;
#elif defined(_WIN32) || defined(_WIN64)
    extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(uint32_t);
    extern "C" __declspec(dllimport) int   __stdcall ReadFile(void*, void*, uint32_t, uint32_t*, void*);
    void* h_in = GetStdHandle(static_cast<uint32_t>(-10) /* STD_INPUT_HANDLE */);
    uint32_t read_bytes = 0;
    if (ReadFile(h_in, buf, static_cast<uint32_t>(count), &read_bytes, nullptr)) {
        return read_bytes;
    }
    return -1;
#else
    (void)fd; (void)buf; (void)count;
    return -1;
#endif
}

int raw_open(const char* pathname, int flags, int mode) {
#if defined(__linux__) && defined(__x86_64__)
    int64_t ret;
    int64_t dfd = -100LL; // AT_FDCWD
    int64_t flg = static_cast<int64_t>(flags);
    int64_t md  = static_cast<int64_t>(mode);

    // Same class of defect that was fixed in raw_mmap: the argument was
    // "r"-constrained and then moved into r10 by hand inside a block that also
    // clobbers r10. Bind it to its ABI register and let the compiler see that.
    register int64_t r10 __asm__("r10") = md;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(257), "D"(dfd), "S"(pathname), "d"(flg), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#else
    (void)pathname; (void)flags; (void)mode;
    return -1;
#endif
}

int raw_close(int fd) {
#if defined(__linux__) && defined(__x86_64__)
    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(3), "D"(static_cast<int64_t>(fd))
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#else
    (void)fd;
    return -1;
#endif
}

int raw_clone(int (*fn)(void*), void* child_stack, int flags, void* arg) {
#if defined(__linux__) && defined(__x86_64__)
    if (!fn || !child_stack) return -1;

    uint64_t* stack = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(child_stack) & ~15UL);
    *(--stack) = reinterpret_cast<uint64_t>(arg);
    *(--stack) = reinterpret_cast<uint64_t>(fn);

    register int64_t rax_reg __asm__("rax") = 56;
    register int64_t rdi_reg __asm__("rdi") = flags;
    register void*   rsi_reg __asm__("rsi") = stack;
    register int64_t rdx_reg __asm__("rdx") = 0;
    register int64_t r10_reg __asm__("r10") = 0;
    register int64_t r8_reg  __asm__("r8")  = 0;

    __asm__ __volatile__(
        "syscall\n\t"
        "testq %%rax, %%rax\n\t"
        "jnz 1f\n\t"
        "popq %%r8\n\t"
        "popq %%rdi\n\t"
        "callq *%%r8\n\t"
        "movq %%rax, %%rdi\n\t"
        "movq $60, %%rax\n\t"
        "syscall\n\t"
        "1:\n\t"
        : "+r"(rax_reg), "+r"(rdi_reg), "+r"(rsi_reg), "+r"(rdx_reg), "+r"(r10_reg), "+r"(r8_reg)
        :
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(rax_reg);
#else
    (void)fn; (void)child_stack; (void)flags; (void)arg;
    return -1;
#endif
}

int raw_futex(int* uaddr, int futex_op, int val, const void* timeout) {
#if defined(__linux__) && defined(__x86_64__)
    int64_t ret;
    int64_t op64 = static_cast<int64_t>(futex_op);
    int64_t val64 = static_cast<int64_t>(val);
    const void* tout = timeout;

    register const void* r10 __asm__("r10") = tout;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(202), "D"(uaddr), "S"(op64), "d"(val64), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#else
    (void)uaddr; (void)futex_op; (void)val; (void)timeout;
    return -1;
#endif
}

int raw_sched_setaffinity(int pid, size_t cpusetsize, const void* mask) {
#if defined(__linux__) && defined(__x86_64__)
    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(203), "D"(static_cast<int64_t>(pid)), "S"(cpusetsize), "d"(mask)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#else
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;
#endif
}

int raw_mbind(void* addr, size_t len, int mode, const void* nodemask, unsigned long maxnode, unsigned flags) {
#if defined(__linux__) && defined(__x86_64__)
    int64_t ret;
    register int64_t r10_reg __asm__("r10") = static_cast<int64_t>(mode);
    register const void* r8_reg __asm__("r8") = nodemask;
    register unsigned long r9_reg __asm__("r9") = maxnode;

    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(237), "D"(addr), "S"(len), "r"(r10_reg), "r"(r8_reg), "r"(r9_reg)
        : "rcx", "r11", "memory"
    );
    (void)flags;
    return static_cast<int>(ret);
#else
    (void)addr; (void)len; (void)mode; (void)nodemask; (void)maxnode; (void)flags;
    return 0;
#endif
}

void raw_exit(int code) {
#if defined(__linux__) && defined(__x86_64__)
    __asm__ __volatile__(
        "movq $60, %%rax\n\t"
        "movq %0, %%rdi\n\t"
        "syscall"
        :
        : "r"((int64_t)code)
        : "rax", "rdi", "memory"
    );
#endif
    while (true) {}
}

void clear_icache(void* addr, size_t size) {
#if defined(__GNUC__) || defined(__clang__)
    char* begin = static_cast<char*>(addr);
    char* end = begin + size;
    __builtin___clear_cache(begin, end);
#endif
}

void* freestanding_memcpy(void* dest, const void* src, size_t n) {
    if (g_memcpy_impl) return g_memcpy_impl(dest, src, n);
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dest;
}

void* freestanding_memset(void* s, int c, size_t n) {
    if (g_memset_impl) return g_memset_impl(s, c, n);
    unsigned char* p = static_cast<unsigned char*>(s);
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<unsigned char>(c);
    return s;
}

void* freestanding_memmove(void* dest, const void* src, size_t n) {
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    if (d < s) {
        return freestanding_memcpy(dest, src, n);
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int freestanding_memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }
    return 0;
}

size_t freestanding_strlen(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

} // namespace sys
} // namespace ana

extern "C" {
    void* memcpy(void* dest, const void* src, size_t n) {
        return ana::sys::freestanding_memcpy(dest, src, n);
    }
    void* memset(void* s, int c, size_t n) {
        return ana::sys::freestanding_memset(s, c, n);
    }
    void* memmove(void* dest, const void* src, size_t n) {
        return ana::sys::freestanding_memmove(dest, src, n);
    }
    int memcmp(const void* s1, const void* s2, size_t n) {
        return ana::sys::freestanding_memcmp(s1, s2, n);
    }
    size_t strlen(const char* s) {
        return ana::sys::freestanding_strlen(s);
    }
    void* malloc(size_t size) {
        if (size == 0) return nullptr;
        // (size + 64 + 4095) wrapped for sizes near SIZE_MAX and returned a
        // tiny mapping in response to an enormous request.
        if (size > (~static_cast<size_t>(0)) - 64 - 4095) return nullptr;
        size_t total_size = (size + 64 + 4095) & ~4095UL;
        void* ptr = ana::sys::raw_mmap(nullptr, total_size, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
        if (ptr == reinterpret_cast<void*>(-1) || !ptr) return nullptr;
        *reinterpret_cast<size_t*>(ptr) = total_size;
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) + 64);
    }
    void free(void* ptr) {
        if (!ptr) return;
        void* raw_ptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - 64);
        size_t total_size = *reinterpret_cast<size_t*>(raw_ptr);
        if (total_size > 0 && (total_size & 4095) == 0 && total_size <= (1024ULL * 1024ULL * 1024ULL)) {
            ana::sys::raw_munmap(raw_ptr, total_size);
        }
    }
    void* realloc(void* ptr, size_t size) {
        if (!ptr) return malloc(size);
        if (size == 0) { free(ptr); return nullptr; }
        void* raw_ptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - 64);
        size_t old_total_size = *reinterpret_cast<size_t*>(raw_ptr);
        size_t old_usable_size = old_total_size - 64;
        if (size <= old_usable_size) return ptr;

        void* new_ptr = malloc(size);
        if (new_ptr) {
            ana::sys::freestanding_memcpy(new_ptr, ptr, old_usable_size);
            free(ptr);
        }
        return new_ptr;
    }
    int getpagesize(void) {
        return 4096;
    }
    long sysconf(int name) {
        (void)name;
        return 4096;
    }
    void abort(void) {
        ana::sys::raw_exit(134);
    }
    char* getenv(const char* name) {
        (void)name;
        return nullptr;
    }
    int open(const char* pathname, int flags, ...) {
        int mode = 0;
        if (flags & 0100) { // O_CREAT
            va_list ap;
            va_start(ap, flags);
            mode = va_arg(ap, int);
            va_end(ap);
        }
        return ana::sys::raw_open(pathname, flags, mode);
    }
    int open64(const char* pathname, int flags, ...) {
        int mode = 0;
        if (flags & 0100) { // O_CREAT
            va_list ap;
            va_start(ap, flags);
            mode = va_arg(ap, int);
            va_end(ap);
        }
        return ana::sys::raw_open(pathname, flags, mode);
    }
    int close(int fd) {
        // This reported success without closing anything, so every descriptor
        // the engine opened stayed open for the life of the process.
        if (fd < 0) return -1;
        return ana::sys::raw_close(fd);
    }
    int ftruncate(int fd, off_t length) {
        (void)fd; (void)length;
        return 0;
    }
    int ftruncate64(int fd, off64_t length) {
        (void)fd; (void)length;
        return 0;
    }
    int64_t read(int fd, void* buf, size_t count) {
        // Returned 0 unconditionally, which every caller correctly reads as
        // "clean end of file".
        return ana::sys::raw_read(fd, buf, count);
    }
    int64_t write(int fd, const void* buf, size_t count) {
        return ana::sys::raw_write(fd, buf, count);
    }
    int64_t sys_raw_write(int fd, const void* buf, size_t count) {
        return ana::sys::raw_write(fd, buf, count);
    }
    void* mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset) {
        return ana::sys::raw_mmap(addr, length, prot, flags, fd, offset);
    }
    int mprotect(void* addr, size_t length, int prot) {
        return ana::sys::raw_mprotect(addr, length, prot);
    }
    int munmap(void* addr, size_t length) {
        return ana::sys::raw_munmap(addr, length);
    }
}



extern "C" {
    // GCC marks these pthread parameters `nonnull`, so a plain `if (!p)` is
    // folded away at -O3: the guard looked present in the source but did not
    // exist in the binary, and it warned under -Wnonnull-compare. Routing the
    // pointer through an empty asm block makes the test real again.
    static inline bool ana_ptr_is_null(const void* p) {
        uintptr_t v;
        __asm__("" : "=r"(v) : "0"(reinterpret_cast<uintptr_t>(p)));
        return v == 0;
    }

    // Every one of these was a no-op that returned success, so all mutual
    // exclusion in the engine was imaginary. Standard three-state futex
    // mutex: 0 = free, 1 = held with no waiters, 2 = held with waiters.
    int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
        (void)attr;
        if (ana_ptr_is_null(mutex)) return -1;
        __atomic_store_n(reinterpret_cast<int*>(mutex), 0, __ATOMIC_RELEASE);
        return 0;
    }
    int pthread_mutex_destroy(pthread_mutex_t* mutex) {
        if (ana_ptr_is_null(mutex)) return -1;
        __atomic_store_n(reinterpret_cast<int*>(mutex), 0, __ATOMIC_RELEASE);
        return 0;
    }
    int pthread_mutex_lock(pthread_mutex_t* mutex) {
        if (ana_ptr_is_null(mutex)) return -1;
        int* w = reinterpret_cast<int*>(mutex);
        int expected = 0;
        if (__atomic_compare_exchange_n(w, &expected, 1, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return 0;
        }
        // Spin briefly before paying for a syscall.
        for (int spin = 0; spin < 64; ++spin) {
            expected = 0;
            if (__atomic_compare_exchange_n(w, &expected, 1, false,
                                            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                return 0;
            }
            __asm__ __volatile__("pause" ::: "memory");
        }
        while (__atomic_exchange_n(w, 2, __ATOMIC_ACQUIRE) != 0) {
            ana::sys::raw_futex(w, ANA_FUTEX_WAIT_PRIVATE, 2, nullptr);
        }
        return 0;
    }
    int pthread_mutex_unlock(pthread_mutex_t* mutex) {
        if (ana_ptr_is_null(mutex)) return -1;
        int* w = reinterpret_cast<int*>(mutex);
        if (__atomic_exchange_n(w, 0, __ATOMIC_RELEASE) == 2) {
            ana::sys::raw_futex(w, ANA_FUTEX_WAKE_PRIVATE, 1, nullptr);
        }
        return 0;
    }
    int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
        // The old version stored the completed value *before* running the
        // initialiser, so a second thread sailed straight past while
        // initialisation was still in flight.
        // 0 = untouched, 1 = running, 2 = complete.
        if (ana_ptr_is_null(once_control)) return -1;
        int* p = reinterpret_cast<int*>(once_control);
        int expected = 0;
        if (__atomic_compare_exchange_n(p, &expected, 1, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            if (!ana_ptr_is_null(reinterpret_cast<const void*>(init_routine))) init_routine();
            __atomic_store_n(p, 2, __ATOMIC_RELEASE);
            ana::sys::raw_futex(p, ANA_FUTEX_WAKE_PRIVATE, 0x7fffffff, nullptr);
            return 0;
        }
        while (__atomic_load_n(p, __ATOMIC_ACQUIRE) != 2) {
            ana::sys::raw_futex(p, ANA_FUTEX_WAIT_PRIVATE, 1, nullptr);
        }
        return 0;
    }
    static const int kMaxTlsKeys = 64;
    static void* g_tls_keys[kMaxTlsKeys] = {nullptr};
    static int g_tls_key_count = 0;

    int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
        (void)destructor;
        if (ana_ptr_is_null(key)) return -1;
        // The counter ran past the end of the table and handed out keys that
        // every later getspecific/setspecific then silently rejected.
        int slot = __atomic_fetch_add(&g_tls_key_count, 1, __ATOMIC_ACQ_REL);
        if (slot >= kMaxTlsKeys) {
            __atomic_fetch_sub(&g_tls_key_count, 1, __ATOMIC_ACQ_REL);
            return 11; // EAGAIN
        }
        g_tls_keys[slot] = nullptr;
        *key = static_cast<pthread_key_t>(slot);
        return 0;
    }
    int pthread_key_delete(pthread_key_t key) {
        if (static_cast<size_t>(key) >= static_cast<size_t>(kMaxTlsKeys)) return -1;
        g_tls_keys[key] = nullptr;
        return 0;
    }
    void* pthread_getspecific(pthread_key_t key) {
        if (static_cast<size_t>(key) < static_cast<size_t>(kMaxTlsKeys)) return g_tls_keys[key];
        return nullptr;
    }
    int pthread_setspecific(pthread_key_t key, const void* value) {
        if (static_cast<size_t>(key) < static_cast<size_t>(kMaxTlsKeys)) {
            g_tls_keys[key] = const_cast<void*>(value);
            return 0;
        }
        return -1;
    }
    long syscall(long number, ...) {
        va_list args;
        va_start(args, number);
        int64_t a1 = va_arg(args, int64_t);
        int64_t a2 = va_arg(args, int64_t);
        int64_t a3 = va_arg(args, int64_t);
        int64_t a4 = va_arg(args, int64_t);
        int64_t a5 = va_arg(args, int64_t);
        int64_t a6 = va_arg(args, int64_t);
        va_end(args);

        register int64_t rax __asm__("rax") = number;
        register int64_t rdi __asm__("rdi") = a1;
        register int64_t rsi __asm__("rsi") = a2;
        register int64_t rdx __asm__("rdx") = a3;
        register int64_t r10 __asm__("r10") = a4;
        register int64_t r8  __asm__("r8")  = a5;
        register int64_t r9  __asm__("r9")  = a6;

        __asm__ __volatile__(
            "syscall"
            : "+r"(rax)
            : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
            : "rcx", "r11", "memory"
        );
        return rax;
    }
    int shm_open(const char* name, int oflag, mode_t mode) {
        (void)name; (void)oflag; (void)mode;
        return -1;
    }
    int shm_unlink(const char* name) {
        (void)name;
        return -1;
    }
    int fputs(const char* s, FILE* stream) {
        (void)stream;
        ana::sys::raw_write(1, s, ana::sys::freestanding_strlen(s));
        return 0;
    }
    // Both of these silently wrote nothing and returned 0, so every caller
    // that formatted a diagnostic produced an empty string.
    int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
        size_t out = 0;
        char tmp[24];

        #define ANA_PUTC(ch) do { \
            if (str && size && out + 1 < size) str[out] = (ch); \
            ++out; \
        } while (0)

        if (!format) { if (str && size) str[0] = '\0'; return 0; }

        for (const char* f = format; *f; ++f) {
            if (*f != '%') { ANA_PUTC(*f); continue; }
            ++f;
            if (*f == '\0') break;
            if (*f == '%') { ANA_PUTC('%'); continue; }

            bool lng = false;
            while (*f == 'l' || *f == 'z' || *f == 'h') { if (*f != 'h') lng = true; ++f; }

            switch (*f) {
                case 'c': ANA_PUTC(static_cast<char>(va_arg(ap, int))); break;
                case 's': {
                    const char* sv = va_arg(ap, const char*);
                    if (!sv) sv = "(null)";
                    while (*sv) ANA_PUTC(*sv++);
                    break;
                }
                case 'd': case 'i': {
                    long long v = lng ? va_arg(ap, long long) : static_cast<long long>(va_arg(ap, int));
                    unsigned long long m = (v < 0) ? (0ULL - static_cast<unsigned long long>(v))
                                                   : static_cast<unsigned long long>(v);
                    if (v < 0) ANA_PUTC('-');
                    int k = 0;
                    do { tmp[k++] = static_cast<char>('0' + (m % 10)); m /= 10; } while (m && k < 24);
                    while (k) ANA_PUTC(tmp[--k]);
                    break;
                }
                case 'u': case 'x': case 'X': case 'p': {
                    unsigned long long m;
                    unsigned bs = (*f == 'u') ? 10u : 16u;
                    if (*f == 'p') { m = reinterpret_cast<unsigned long long>(va_arg(ap, void*)); ANA_PUTC('0'); ANA_PUTC('x'); }
                    else m = lng ? va_arg(ap, unsigned long long)
                                 : static_cast<unsigned long long>(va_arg(ap, unsigned int));
                    const char* digits = (*f == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    int k = 0;
                    do { tmp[k++] = digits[m % bs]; m /= bs; } while (m && k < 24);
                    while (k) ANA_PUTC(tmp[--k]);
                    break;
                }
                default: ANA_PUTC('%'); ANA_PUTC(*f); break;
            }
        }

        #undef ANA_PUTC

        if (str && size) str[out < size ? out : size - 1] = '\0';
        return static_cast<int>(out); // C99: length that would have been written
    }
    int snprintf(char* str, size_t size, const char* format, ...) {
        va_list ap;
        va_start(ap, format);
        int r = vsnprintf(str, size, format, ap);
        va_end(ap);
        return r;
    }
    int uname(struct utsname* buf) {
        if (!buf) return -1;
        ana::sys::freestanding_memset(buf, 0, sizeof(struct utsname));
        ana::sys::freestanding_memcpy(buf->sysname, "Linux", 6);
        ana::sys::freestanding_memcpy(buf->release, "6.1.0", 6);
        return 0;
    }
    long strtol(const char* nptr, char** endptr, int base) {
        // The old version ignored `base` entirely, accepted no sign, and had
        // no overflow handling: strtol("-42") returned 0.
        if (endptr) *endptr = const_cast<char*>(nptr);
        if (!nptr) return 0;
        if (base != 0 && (base < 2 || base > 36)) return 0;

        const char* p = nptr;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
               *p == '\v' || *p == '\f') p++;

        bool neg = false;
        if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

        if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            int hv = -1;
            char h = p[2];
            if (h >= '0' && h <= '9') hv = h - '0';
            else if (h >= 'a' && h <= 'f') hv = h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') hv = h - 'A' + 10;
            if (hv >= 0) { base = 16; p += 2; }
            else if (base == 0) { base = 10; }
        } else if (base == 0) {
            base = (p[0] == '0' && p[1] != '\0') ? 8 : 10;
        }

        const unsigned long ulmax = ~0UL;
        const unsigned long cutoff = neg ? (ulmax / 2 + 1) : (ulmax / 2);
        unsigned long acc = 0;
        bool any = false, ovf = false;

        for (;; ++p) {
            int d;
            char c = *p;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
            else break;
            if (d >= base) break;

            any = true;
            if (acc > (cutoff - static_cast<unsigned long>(d)) / static_cast<unsigned long>(base)) {
                ovf = true;
            } else {
                acc = acc * static_cast<unsigned long>(base) + static_cast<unsigned long>(d);
            }
        }

        if (!any) return 0;
        if (endptr) *endptr = const_cast<char*>(p);
        if (ovf) return neg ? (-static_cast<long>(ulmax / 2) - 1) : static_cast<long>(ulmax / 2);
        return neg ? -static_cast<long>(acc) : static_cast<long>(acc);
    }
    long __isoc23_strtol(const char* nptr, char** endptr, int base) {
        return strtol(nptr, endptr, base);
    }
}
