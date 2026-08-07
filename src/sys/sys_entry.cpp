#include "sys_raw.h"
#include "cpu_features.h"

extern int ana_main(int argc, char** argv);

// Freestanding C++ operator new & delete overrides using bare-metal malloc/free
void* operator new(size_t size) {
    return malloc(size);
}

void* operator new[](size_t size) {
    return malloc(size);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    free(ptr);
}

namespace __cxxabiv1 {
    class __class_type_info {
    public:
        virtual ~__class_type_info();
    };
    __class_type_info::~__class_type_info() {}

    class __si_class_type_info : public __class_type_info {
    public:
        virtual ~__si_class_type_info();
    };
    __si_class_type_info::~__si_class_type_info() {}
}

extern "C" {
    int __popcountdi2(uint64_t a) {
        int count = 0;
        while (a) {
            count += (a & 1);
            a >>= 1;
        }
        return count;
    }
    void _Unwind_Resume(void*) {
        ana::sys::raw_exit(134);
    }
    int __gxx_personality_v0 = 0;
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void*), void*, void*) {
        return 0;
    }
    int __cxa_guard_acquire(int64_t* guard) {
        if (*reinterpret_cast<volatile char*>(guard) == 0) return 1;
        return 0;
    }
    void __cxa_guard_release(int64_t* guard) {
        *reinterpret_cast<volatile char*>(guard) = 1;
    }
    void __cxa_guard_abort(int64_t* guard) {
        (void)guard;
    }
}

extern "C" {

// Freestanding stack cookie overrides
uintptr_t __stack_chk_guard = 0xdeadbeefcafebabeULL;
uintptr_t __security_cookie = 0xdeadbeefcafebabeULL;

void __stack_chk_fail(void) {
    ana::sys::raw_write(2, "Stack check failed!\n", 20);
    ana::sys::raw_exit(137);
}

void __chkstk(void) {}

#if defined(__ELF__) || defined(__linux__)
extern "C" __attribute__((noreturn)) void _start_c(int argc, char** argv) {
    ana::sys::detect_cpu_features();
    int result = ana_main(argc, argv);
    ana::sys::raw_exit(result);
}

__asm__(
    ".global _start\n"
    "_start:\n"
    "    mov (%rsp), %rdi\n"
    "    lea 8(%rsp), %rsi\n"
    "    jmp _start_c\n"
);
#elif defined(_WIN32)
int mainCRTStartup() {
    ana::sys::detect_cpu_features();
    int result = ana_main(0, nullptr);
    ana::sys::raw_exit(result);
    return result;
}
#endif

} // extern "C"
