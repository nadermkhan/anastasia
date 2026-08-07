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

    __asm__ __volatile__(
        "movq %5, %%r10\n\t"
        "movq %6, %%r8\n\t"
        "movq %7, %%r9\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(9), "D"(addr), "S"(length), "d"(prot), "r"(flg), "r"(f), "r"(off)
        : "r10", "r8", "r9", "rcx", "r11", "memory"
    );

    if (static_cast<uintptr_t>(ret) >= static_cast<uintptr_t>(-4095UL)) {
        return reinterpret_cast<void*>(-1);
    }
    return reinterpret_cast<void*>(ret);
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

    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(10), "D"(aligned_start), "S"(aligned_len), "d"(prot)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
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

    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(11), "D"(aligned_start), "S"(aligned_len)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#else
    (void)addr; (void)length;
    return -1;
#endif
}

int64_t raw_write(int fd, const void* buf, size_t count) {
#if defined(__linux__) && defined(__x86_64__)
    int64_t ret;
    int64_t fd64 = static_cast<int64_t>(fd);
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(1), "D"(fd64), "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
#else
    (void)fd; (void)buf; (void)count;
    return -1;
#endif
}

int64_t raw_read(int fd, void* buf, size_t count) {
#if defined(__linux__) && defined(__x86_64__)
    int64_t ret;
    int64_t fd64 = static_cast<int64_t>(fd);
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(0), "D"(fd64), "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
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

    __asm__ __volatile__(
        "movq %5, %%r10\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(257), "D"(dfd), "S"(pathname), "d"(flg), "r"(md)
        : "r10", "rcx", "r11", "memory"
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

    __asm__ __volatile__(
        "movq %5, %%r10\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(202), "D"(uaddr), "S"(op64), "d"(val64), "r"(tout)
        : "r10", "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#else
    (void)uaddr; (void)futex_op; (void)val; (void)timeout;
    return -1;
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
        (void)pathname; (void)flags;
        return -1;
    }
    int open64(const char* pathname, int flags, ...) {
        (void)pathname; (void)flags;
        return -1;
    }
    int close(int fd) {
        (void)fd;
        return 0;
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
        (void)fd; (void)buf; (void)count;
        return 0;
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
    int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
        (void)mutex; (void)attr;
        return 0;
    }
    int pthread_mutex_destroy(pthread_mutex_t* mutex) {
        (void)mutex;
        return 0;
    }
    int pthread_mutex_lock(pthread_mutex_t* mutex) {
        (void)mutex;
        return 0;
    }
    int pthread_mutex_unlock(pthread_mutex_t* mutex) {
        (void)mutex;
        return 0;
    }
    int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
        int* p = reinterpret_cast<int*>(once_control);
        if (p && *p == 0) {
            *p = 2;
            if (init_routine) init_routine();
        }
        return 0;
    }
    static void* g_tls_keys[64] = {nullptr};
    static int g_tls_key_count = 0;

    int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
        (void)destructor;
        if (!key) return -1;
        *key = static_cast<pthread_key_t>(g_tls_key_count++);
        return 0;
    }
    int pthread_key_delete(pthread_key_t key) {
        (void)key;
        return 0;
    }
    void* pthread_getspecific(pthread_key_t key) {
        if (static_cast<size_t>(key) < 64) return g_tls_keys[key];
        return nullptr;
    }
    int pthread_setspecific(pthread_key_t key, const void* value) {
        if (static_cast<size_t>(key) < 64) {
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
    int snprintf(char* str, size_t size, const char* format, ...) {
        (void)str; (void)size; (void)format;
        return 0;
    }
    int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
        (void)str; (void)size; (void)format; (void)ap;
        return 0;
    }
    int uname(struct utsname* buf) {
        if (!buf) return -1;
        ana::sys::freestanding_memset(buf, 0, sizeof(struct utsname));
        ana::sys::freestanding_memcpy(buf->sysname, "Linux", 6);
        ana::sys::freestanding_memcpy(buf->release, "6.1.0", 6);
        return 0;
    }
    long strtol(const char* nptr, char** endptr, int base) {
        (void)base;
        if (!nptr) return 0;
        const char* p = nptr;
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        long res = 0;
        while (*p >= '0' && *p <= '9') {
            res = res * 10 + (*p - '0');
            p++;
        }
        if (endptr) *endptr = const_cast<char*>(p);
        return res;
    }
    long __isoc23_strtol(const char* nptr, char** endptr, int base) {
        return strtol(nptr, endptr, base);
    }
}
