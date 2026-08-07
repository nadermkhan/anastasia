#include "sys_raw.h"
#include "cpu_features.h"

extern "C" {
    void* malloc(size_t size);
    void  free(void* ptr);
}

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace ana {
namespace sys {

static CpuFeatures g_features = {false, false, false, false};

// Scalar implementations
static void* memcpy_scalar(void* dest, const void* src, size_t n) {
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

static void* memset_scalar(void* s, int c, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(s);
    for (size_t i = 0; i < n; ++i) {
        p[i] = static_cast<unsigned char>(c);
    }
    return s;
}

#if defined(__x86_64__) || defined(_M_X64)
__attribute__((target("avx2")))
static void* memcpy_avx2(void* dest, const void* src, size_t n) {
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    size_t i = 0;
    while (i + 32 <= n) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i), chunk);
        i += 32;
    }
    while (i < n) {
        d[i] = s[i];
        i++;
    }
    return dest;
}

__attribute__((target("avx2")))
static void* memset_avx2(void* s, int c, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(s);
    size_t i = 0;
    __m256i val = _mm256_set1_epi8(static_cast<char>(c));
    while (i + 32 <= n) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i), val);
        i += 32;
    }
    while (i < n) {
        p[i] = static_cast<unsigned char>(c);
        i++;
    }
    return s;
}
#endif

memcpy_fn_t g_memcpy_impl = memcpy_scalar;
memset_fn_t g_memset_impl = memset_scalar;

void detect_cpu_features() {
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    // CPUID EAX=1
    __asm__ __volatile__(
        "movq %%rbx, %%r11\n\t"
        "cpuid\n\t"
        "movl %%ebx, %1\n\t"
        "movq %%r11, %%rbx"
        : "=a"(eax), "=r"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1), "c"(0)
        : "r11"
    );
    g_features.sse2 = (edx & (1 << 26)) != 0;

    // CPUID EAX=7, ECX=0
    __asm__ __volatile__(
        "movq %%rbx, %%r11\n\t"
        "cpuid\n\t"
        "movl %%ebx, %1\n\t"
        "movq %%r11, %%rbx"
        : "=a"(eax), "=r"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0)
        : "r11"
    );
    g_features.avx2 = (ebx & (1 << 5)) != 0;
    g_features.avx512f = (ebx & (1 << 16)) != 0;

    g_memcpy_impl = memcpy_scalar;
    g_memset_impl = memset_scalar;
#elif defined(__aarch64__) || defined(_M_ARM64)
    g_features.neon = true;
    g_memcpy_impl = memcpy_scalar;
    g_memset_impl = memset_scalar;
#else
    g_memcpy_impl = memcpy_scalar;
    g_memset_impl = memset_scalar;
#endif
}

const CpuFeatures& get_cpu_features() {
    return g_features;
}

} // namespace sys
} // namespace ana
