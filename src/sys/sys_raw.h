#ifndef ANA_SYS_RAW_H
#define ANA_SYS_RAW_H

#include <stddef.h>
#include <stdint.h>

#define ANA_PROT_NONE  0x0
#define ANA_PROT_READ  0x1
#define ANA_PROT_WRITE 0x2
#define ANA_PROT_EXEC  0x4

#define ANA_MAP_PRIVATE   0x02
#define ANA_MAP_ANONYMOUS 0x20

#define ANA_CLONE_VM       0x00000100
#define ANA_CLONE_FS       0x00000200
#define ANA_CLONE_FILES    0x00000400
#define ANA_CLONE_SIGHAND  0x00000800
#define ANA_CLONE_THREAD   0x00001000
#define ANA_CLONE_SETTLS   0x00080000

#if defined(_MSC_VER)
#define ANA_NORETURN __declspec(noreturn)
#include <intrin.h>

#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

template <typename T>
inline T __atomic_load_n(const volatile T* ptr, int memorder) {
    (void)memorder;
    T val = *ptr;
    _ReadWriteBarrier();
    return val;
}

template <typename T, typename ValT>
inline void __atomic_store_n(volatile T* ptr, ValT val, int memorder) {
    (void)memorder;
    _ReadWriteBarrier();
    *ptr = static_cast<T>(val);
    _ReadWriteBarrier();
}

template <typename T, typename ValT>
inline T __atomic_fetch_add(volatile T* ptr, ValT val, int memorder) {
    (void)memorder;
    if (sizeof(T) == 4) {
        return static_cast<T>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), static_cast<long>(val)));
    } else {
        return static_cast<T>(_InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(ptr), static_cast<long long>(val)));
    }
}

template <typename T, typename ValT>
inline bool __atomic_compare_exchange_n(volatile T* ptr, T* expected, ValT desired, bool weak, int success_memorder, int failure_memorder) {
    (void)weak; (void)success_memorder; (void)failure_memorder;
    if (sizeof(T) == 1) {
        char old = _InterlockedCompareExchange8(reinterpret_cast<volatile char*>(ptr), static_cast<char>(desired), *reinterpret_cast<char*>(expected));
        if (old == *reinterpret_cast<char*>(expected)) return true;
        *expected = static_cast<T>(old);
        return false;
    } else if (sizeof(T) == 4) {
        long old = _InterlockedCompareExchange(reinterpret_cast<volatile long*>(ptr), static_cast<long>(desired), *reinterpret_cast<long*>(expected));
        if (old == *reinterpret_cast<long*>(expected)) return true;
        *expected = static_cast<T>(old);
        return false;
    } else {
        long long old = _InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(ptr), static_cast<long long>(desired), *reinterpret_cast<long long*>(expected));
        if (old == *reinterpret_cast<long long*>(expected)) return true;
        *expected = static_cast<T>(old);
        return false;
    }
}
#else
#define ANA_NORETURN __attribute__((noreturn))
#endif

#define ANA_FUTEX_WAIT         0
#define ANA_FUTEX_WAKE         1
#define ANA_FUTEX_PRIVATE_FLAG 128
#define ANA_FUTEX_WAIT_PRIVATE (ANA_FUTEX_WAIT | ANA_FUTEX_PRIVATE_FLAG)
#define ANA_FUTEX_WAKE_PRIVATE (ANA_FUTEX_WAKE | ANA_FUTEX_PRIVATE_FLAG)

namespace ana {
namespace sys {

// Raw syscall wrappers (Linux) / PEB-based Win32 APIs (Windows)
void* raw_mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset);
int   raw_mprotect(void* addr, size_t length, int prot);
int   raw_munmap(void* addr, size_t length);
int64_t raw_write(int fd, const void* buf, size_t count);
int64_t raw_read(int fd, void* buf, size_t count);
int   raw_open(const char* pathname, int flags, int mode);
int   raw_close(int fd);
int   raw_clone(int (*fn)(void*), void* child_stack, int flags, void* arg);
int   raw_futex(int* uaddr, int futex_op, int val, const void* timeout = nullptr);
int   raw_sched_setaffinity(int pid, size_t cpusetsize, const void* mask);
int   raw_mbind(void* addr, size_t len, int mode, const void* nodemask, unsigned long maxnode, unsigned flags);
ANA_NORETURN void raw_exit(int code);
void  clear_icache(void* addr, size_t size);
void  spinlock_yield();

// Freestanding memory routines
void* freestanding_memcpy(void* dest, const void* src, size_t n);
void* freestanding_memset(void* s, int c, size_t n);
void* freestanding_memmove(void* dest, const void* src, size_t n);
int   freestanding_memcmp(const void* s1, const void* s2, size_t n);
size_t freestanding_strlen(const char* s);

} // namespace sys
} // namespace ana

#ifndef _WIN32
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <stdio.h>

// Global symbol overrides for compiler implicit calls and AsmJit OS utilities
extern "C" {
    void* memcpy(void* dest, const void* src, size_t n);
    void* memset(void* s, int c, size_t n);
    void* memmove(void* dest, const void* src, size_t n);
    int   memcmp(const void* s1, const void* s2, size_t n);
    size_t strlen(const char* s);
    void* malloc(size_t size);
    void* realloc(void* ptr, size_t size);
    void  free(void* ptr);
    int   getpagesize(void);
    long  sysconf(int name);
    void  abort(void);
    char* getenv(const char* name);
    int   open(const char* pathname, int flags, ...);
    int   open64(const char* pathname, int flags, ...);
    int   close(int fd);
    int   ftruncate(int fd, off_t length);
    int   ftruncate64(int fd, off64_t length);
    int64_t read(int fd, void* buf, size_t count);
    int64_t write(int fd, const void* buf, size_t count);
    int64_t sys_raw_write(int fd, const void* buf, size_t count);
    void* mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset);
    int   mprotect(void* addr, size_t length, int prot);
    int   munmap(void* addr, size_t length);
    int   pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
    int   pthread_mutex_destroy(pthread_mutex_t* mutex);
    int   pthread_mutex_lock(pthread_mutex_t* mutex);
    int   pthread_mutex_unlock(pthread_mutex_t* mutex);
    int   pthread_once(pthread_once_t* once_control, void (*init_routine)(void));
    int   pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
    int   pthread_key_delete(pthread_key_t key);
    void* pthread_getspecific(pthread_key_t key);
    int   pthread_setspecific(pthread_key_t key, const void* value);
    long  syscall(long number, ...);
    int   shm_open(const char* name, int oflag, mode_t mode);
    int   shm_unlink(const char* name);
    int   fputs(const char* s, FILE* stream);
    int   snprintf(char* str, size_t size, const char* format, ...);
    int   vsnprintf(char* str, size_t size, const char* format, va_list ap);
    int   uname(struct utsname* buf);
    long  strtol(const char* nptr, char** endptr, int base);
    long  __isoc23_strtol(const char* nptr, char** endptr, int base);
}
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#endif // ANA_SYS_RAW_H
