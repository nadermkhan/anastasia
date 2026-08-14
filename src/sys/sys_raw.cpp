#include "sys_raw.h"
#include "cpu_features.h"
#include <stdarg.h>

#if defined(_WIN32)
#include <windows.h>
typedef int mode_t;
#endif

#ifndef _WIN32
extern "C" {
void* malloc(size_t size) {
    if (size == 0) size = 1;
    return ana::sys::raw_mmap(nullptr, size, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
}

void free(void* ptr) {
    if (ptr && ptr != (void*)-1) {
        (void)ana::sys::raw_munmap(ptr, 4096);
    }
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return nullptr;
    }
    void* new_ptr = malloc(size);
    if (new_ptr && new_ptr != (void*)-1) {
        ana::sys::freestanding_memcpy(new_ptr, ptr, size);
        free(ptr);
    }
    return new_ptr;
}

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
}
#endif

namespace ana {
namespace sys {

void* freestanding_memcpy(void* dest, const void* src, size_t n) {
#if defined(_WIN32)
    return ::memcpy(dest, src, n);
#else
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dest;
#endif
}

void* freestanding_memset(void* s, int c, size_t n) {
#if defined(_WIN32)
    return ::memset(s, c, n);
#else
    unsigned char* p = static_cast<unsigned char*>(s);
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<unsigned char>(c);
    return s;
#endif
}

void* freestanding_memmove(void* dest, const void* src, size_t n) {
#if defined(_WIN32)
    return ::memmove(dest, src, n);
#else
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dest;
#endif
}

int freestanding_memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

size_t freestanding_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len] != '\0') ++len;
    return len;
}

void clear_icache(void* addr, size_t size) {
#if defined(_MSC_VER)
    (void)addr; (void)size;
#elif defined(__x86_64__) || defined(_M_X64)
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t end = start + size;
    for (uintptr_t p = start & ~63UL; p < end; p += 64) {
        __asm__ __volatile__("clflush (%0)" :: "r"(p) : "memory");
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    (void)addr; (void)size;
    __asm__ __volatile__("isb" ::: "memory");
#else
    (void)addr; (void)size;
#endif
}

void* raw_mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    if ((prot & ANA_PROT_WRITE) && (prot & ANA_PROT_EXEC)) {
        prot &= ~ANA_PROT_EXEC;
    }

    int64_t ret;
    int64_t flg = static_cast<int64_t>(flags);
    int64_t f   = static_cast<int64_t>(fd);
    int64_t off = static_cast<int64_t>(offset);

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
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    if ((prot & ANA_PROT_WRITE) && (prot & ANA_PROT_EXEC)) {
        prot &= ~ANA_PROT_EXEC;
    }
    register int64_t x8 __asm__("x8") = 222; // __NR_mmap
    register int64_t x0 __asm__("x0") = reinterpret_cast<int64_t>(addr);
    register int64_t x1 __asm__("x1") = static_cast<int64_t>(length);
    register int64_t x2 __asm__("x2") = static_cast<int64_t>(prot);
    register int64_t x3 __asm__("x3") = static_cast<int64_t>(flags);
    register int64_t x4 __asm__("x4") = static_cast<int64_t>(fd);
    register int64_t x5 __asm__("x5") = static_cast<int64_t>(offset);

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory"
    );

    if (static_cast<uintptr_t>(x0) >= static_cast<uintptr_t>(-4095UL)) {
        return reinterpret_cast<void*>(-1);
    }
    return reinterpret_cast<void*>(x0);
#elif defined(_WIN32) || defined(_WIN64)
    (void)flags; (void)fd; (void)offset;
    DWORD win_prot = PAGE_READWRITE;
    if (prot & ANA_PROT_EXEC) win_prot = PAGE_EXECUTE_READWRITE;
    void* ptr = VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, win_prot);
    return ptr ? ptr : reinterpret_cast<void*>(-1);
#else
    (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    return reinterpret_cast<void*>(-1);
#endif
}

int raw_mprotect(void* addr, size_t length, int prot) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t page_size = 4096;
    uintptr_t aligned_start = start & ~(page_size - 1);
    size_t aligned_len = length + (start - aligned_start);
    aligned_len = (aligned_len + (page_size - 1)) & ~(page_size - 1);

    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(10), "D"(aligned_start), "S"(aligned_len), "d"(prot)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t page_size = 4096;
    uintptr_t aligned_start = start & ~(page_size - 1);
    size_t aligned_len = length + (start - aligned_start);
    aligned_len = (aligned_len + (page_size - 1)) & ~(page_size - 1);

    register int64_t x8 __asm__("x8") = 226; // __NR_mprotect
    register int64_t x0 __asm__("x0") = static_cast<int64_t>(aligned_start);
    register int64_t x1 __asm__("x1") = static_cast<int64_t>(aligned_len);
    register int64_t x2 __asm__("x2") = static_cast<int64_t>(prot);

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory"
    );
    return static_cast<int>(x0);
#elif defined(_WIN32) || defined(_WIN64)
    DWORD win_prot = PAGE_READWRITE;
    if ((prot & ANA_PROT_READ) && (prot & ANA_PROT_EXEC)) win_prot = PAGE_EXECUTE_READ;
    DWORD old_prot = 0;
    return VirtualProtect(addr, length, win_prot, &old_prot) ? 0 : -1;
#else
    (void)addr; (void)length; (void)prot;
    return -1;
#endif
}

int raw_munmap(void* addr, size_t length) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t page_size = 4096;
    uintptr_t aligned_start = start & ~(page_size - 1);
    size_t aligned_len = length + (start - aligned_start);
    aligned_len = (aligned_len + (page_size - 1)) & ~(page_size - 1);

    register int64_t x8 __asm__("x8") = 215; // __NR_munmap
    register int64_t x0 __asm__("x0") = static_cast<int64_t>(aligned_start);
    register int64_t x1 __asm__("x1") = static_cast<int64_t>(aligned_len);

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1)
        : "memory"
    );
    return static_cast<int>(x0);
#elif defined(_WIN32) || defined(_WIN64)
    (void)length;
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
#else
    (void)addr; (void)length;
    return -1;
#endif
}

int64_t raw_write(int fd, const void* buf, size_t count) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    const unsigned char* p = static_cast<const unsigned char*>(buf);
    size_t done = 0;
    while (done < count) {
        const void* cur = static_cast<const void*>(p + done);
        size_t remaining = count - done;
        register int64_t x8 __asm__("x8") = 64; // __NR_write
        register int64_t x0 __asm__("x0") = static_cast<int64_t>(fd);
        register int64_t x1 __asm__("x1") = reinterpret_cast<int64_t>(cur);
        register int64_t x2 __asm__("x2") = static_cast<int64_t>(remaining);

        __asm__ __volatile__(
            "svc #0"
            : "+r"(x0)
            : "r"(x8), "r"(x1), "r"(x2)
            : "memory"
        );
        if (x0 < 0) {
            if (x0 == -4) continue; // EINTR
            return done ? static_cast<int64_t>(done) : x0;
        }
        if (x0 == 0) break;
        done += static_cast<size_t>(x0);
    }
    return static_cast<int64_t>(done);
#elif defined(_WIN32) || defined(_WIN64)
    HANDLE h = (fd == 1) ? GetStdHandle(STD_OUTPUT_HANDLE) :
               (fd == 2) ? GetStdHandle(STD_ERROR_HANDLE) : reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
    DWORD written = 0;
    WriteFile(h, buf, static_cast<DWORD>(count), &written, NULL);
    return static_cast<int64_t>(written);
#else
    (void)fd; (void)buf; (void)count;
    return -1;
#endif
}

int64_t raw_writev(int fd, const raw_iovec* iov, int iovcnt) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    int64_t ret;
    int64_t fd64 = static_cast<int64_t>(fd);
    int64_t cnt64 = static_cast<int64_t>(iovcnt);
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(20), "D"(fd64), "S"(iov), "d"(cnt64)
        : "rcx", "r11", "memory"
    );
    return ret;
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    register int64_t x8 __asm__("x8") = 66; // __NR_writev
    register int64_t x0 __asm__("x0") = static_cast<int64_t>(fd);
    register int64_t x1 __asm__("x1") = reinterpret_cast<int64_t>(iov);
    register int64_t x2 __asm__("x2") = static_cast<int64_t>(iovcnt);

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory"
    );
    return x0;
#else
    int64_t total = 0;
    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_base && iov[i].iov_len > 0) {
            int64_t res = raw_write(fd, iov[i].iov_base, iov[i].iov_len);
            if (res > 0) total += res;
        }
    }
    return total;
#endif
}

int64_t raw_read(int fd, void* buf, size_t count) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    for (;;) {
        register int64_t x8 __asm__("x8") = 63; // __NR_read
        register int64_t x0 __asm__("x0") = static_cast<int64_t>(fd);
        register int64_t x1 __asm__("x1") = reinterpret_cast<int64_t>(buf);
        register int64_t x2 __asm__("x2") = static_cast<int64_t>(count);
        __asm__ __volatile__(
            "svc #0"
            : "+r"(x0)
            : "r"(x8), "r"(x1), "r"(x2)
            : "memory"
        );
        if (x0 == -4) continue;
        return x0;
    }
#elif defined(_WIN32) || defined(_WIN64)
    (void)fd;
    HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD read_bytes = 0;
    if (ReadFile(h_in, buf, static_cast<DWORD>(count), &read_bytes, nullptr)) {
        return read_bytes;
    }
    return -1;
#else
    (void)fd; (void)buf; (void)count;
    return -1;
#endif
}

int raw_open(const char* pathname, int flags, int mode) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    int64_t ret;
    int64_t dfd = -100LL; // AT_FDCWD
    int64_t flg = static_cast<int64_t>(flags);
    int64_t md  = static_cast<int64_t>(mode);

    register int64_t r10 __asm__("r10") = md;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(257), "D"(dfd), "S"(pathname), "d"(flg), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    register int64_t x8 __asm__("x8") = 56; // __NR_openat
    register int64_t x0 __asm__("x0") = -100LL; // AT_FDCWD
    register int64_t x1 __asm__("x1") = reinterpret_cast<int64_t>(pathname);
    register int64_t x2 __asm__("x2") = static_cast<int64_t>(flags);
    register int64_t x3 __asm__("x3") = static_cast<int64_t>(mode);

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
        : "memory"
    );
    return static_cast<int>(x0);
#else
    (void)pathname; (void)flags; (void)mode;
    return -1;
#endif
}

int raw_close(int fd) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(3), "D"(static_cast<int64_t>(fd))
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    register int64_t x8 __asm__("x8") = 57; // __NR_close
    register int64_t x0 __asm__("x0") = static_cast<int64_t>(fd);

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory"
    );
    return static_cast<int>(x0);
#else
    (void)fd;
    return -1;
#endif
}

int raw_clone(int (*fn)(void*), void* child_stack, int flags, void* arg) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    if (!fn || !child_stack) return -1;
    uint64_t* stack = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(child_stack) & ~15UL);
    *(--stack) = reinterpret_cast<uint64_t>(fn);  // offset 8 -> loaded into x1 by ldp
    *(--stack) = reinterpret_cast<uint64_t>(arg); // offset 0 -> loaded into x0 by ldp

    register int64_t x8_reg __asm__("x8") = 220; // __NR_clone
    register int64_t x0_reg __asm__("x0") = flags;
    register void*   x1_reg __asm__("x1") = stack;

    __asm__ __volatile__(
        "svc #0\n\t"
        "cbnz x0, 1f\n\t"
        "ldp x0, x1, [sp], #16\n\t"
        "blr x1\n\t"
        "mov x8, #93\n\t"
        "svc #0\n\t"
        "1:\n\t"
        : "+r"(x0_reg)
        : "r"(x8_reg), "r"(x1_reg)
        : "memory"
    );
    return static_cast<int>(x0_reg);
#else
    (void)fn; (void)child_stack; (void)flags; (void)arg;
    return -1;
#endif
}

int raw_futex(int* uaddr, int futex_op, int val, const void* timeout) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    register int64_t x8 __asm__("x8") = 98; // __NR_futex
    register int64_t x0 __asm__("x0") = reinterpret_cast<int64_t>(uaddr);
    register int64_t x1 __asm__("x1") = static_cast<int64_t>(futex_op);
    register int64_t x2 __asm__("x2") = static_cast<int64_t>(val);
    register int64_t x3 __asm__("x3") = reinterpret_cast<int64_t>(timeout);

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
        : "memory"
    );
    return static_cast<int>(x0);
#else
    (void)uaddr; (void)futex_op; (void)val; (void)timeout;
    return -1;
#endif
}

int raw_sched_setaffinity(int pid, size_t cpusetsize, const void* mask) {
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    __asm__ __volatile__(
        "movq $60, %%rax\n\t"
        "movq %0, %%rdi\n\t"
        "syscall"
        :
        : "r"((int64_t)code)
        : "rax", "rdi", "memory"
    );
    __builtin_unreachable();
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    register int64_t x8 __asm__("x8") = 93; // __NR_exit
    register int64_t x0 __asm__("x0") = code;
    __asm__ __volatile__(
        "svc #0"
        :
        : "r"(x8), "r"(x0)
        : "memory"
    );
    __builtin_unreachable();
#elif defined(_WIN32)
    ExitProcess(static_cast<uint32_t>(code));
#else
    (void)code;
#endif
}

void spinlock_yield() {
#if defined(_MSC_VER)
    _mm_pause();
#elif defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("pause" ::: "memory");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield" ::: "memory");
#endif
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

#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
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
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    register int64_t x8 __asm__("x8") = number;
    register int64_t x0 __asm__("x0") = a1;
    register int64_t x1 __asm__("x1") = a2;
    register int64_t x2 __asm__("x2") = a3;
    register int64_t x3 __asm__("x3") = a4;
    register int64_t x4 __asm__("x4") = a5;
    register int64_t x5 __asm__("x5") = a6;

    __asm__ __volatile__(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory"
    );
    return x0;
#else
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return -1;
#endif
}

int shm_open(const char* name, int oflag, mode_t mode) {
    (void)name; (void)oflag; (void)mode;
    return -1;
}

int shm_unlink(const char* name) {
    (void)name;
    return -1;
}

} // namespace sys
} // namespace ana
