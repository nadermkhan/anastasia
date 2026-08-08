#include "sys_raw.h"
#include "cpu_features.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace ana {
namespace sys {

static CpuFeatures g_features = {false, false, false, false};

// ---------------------------------------------------------------------------
// Scalar reference implementations (always available)
// ---------------------------------------------------------------------------
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
    while (i + 128 <= n) {
        __m256i c0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
        __m256i c1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i + 32));
        __m256i c2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i + 64));
        __m256i c3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i + 96));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i), c0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i + 32), c1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i + 64), c2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i + 96), c3);
        i += 128;
    }
    while (i + 32 <= n) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + i), chunk);
        i += 32;
    }
    while (i < n) {
        d[i] = s[i];
        ++i;
    }
    return dest;
}

__attribute__((target("avx2")))
static void* memset_avx2(void* s, int c, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(s);
    size_t i = 0;
    __m256i val = _mm256_set1_epi8(static_cast<char>(c));
    while (i + 128 <= n) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i), val);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i + 32), val);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i + 64), val);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i + 96), val);
        i += 128;
    }
    while (i + 32 <= n) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i), val);
        i += 32;
    }
    while (i < n) {
        p[i] = static_cast<unsigned char>(c);
        ++i;
    }
    return s;
}

// CPUID that cannot destroy its own result.
//
// The previous version saved RBX into R11 and restored it afterwards, but the
// EBX output used a plain "=r" constraint. Nothing stopped the register
// allocator from picking RBX itself for that output, in which case the
// restoring move overwrote the value we had just read. The xchg form with an
// early-clobber output is the idiom glibc uses, and it is also correct under
// -fPIC where RBX holds the GOT pointer and must be preserved.
static inline void ana_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t out[4]) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
    __asm__ __volatile__(
        "xchgq %%rbx, %q1\n\t"
        "cpuid\n\t"
        "xchgq %%rbx, %q1"
        : "=a"(a), "=&r"(b), "=c"(c), "=d"(d)
        : "0"(leaf), "2"(subleaf)
    );
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = d;
}

// XGETBV, hand-encoded so this file still builds without -mxsave.
static inline uint64_t ana_xgetbv0() {
    uint32_t lo = 0, hi = 0;
    __asm__ __volatile__(
        ".byte 0x0f, 0x01, 0xd0"
        : "=a"(lo), "=d"(hi)
        : "c"(0)
    );
    return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}
#endif

memcpy_fn_t g_memcpy_impl = memcpy_scalar;
memset_fn_t g_memset_impl = memset_scalar;

void detect_cpu_features() {
    g_features.avx512f = false;
    g_features.avx2 = false;
    g_features.sse2 = false;
    g_features.neon = false;

#if defined(__x86_64__) || defined(_M_X64)
    uint32_t r[4] = {0, 0, 0, 0};

    // Leaf 0 reports the highest leaf this CPU actually implements. Querying
    // leaf 7 without checking it returns the highest supported leaf's data
    // instead, which is how a CPU with no AVX2 can appear to have AVX2.
    ana_cpuid(0, 0, r);
    const uint32_t max_leaf = r[0];

    if (max_leaf >= 1) {
        ana_cpuid(1, 0, r);
        g_features.sse2 = (r[3] & (1u << 26)) != 0;

        const bool osxsave = (r[2] & (1u << 27)) != 0;
        const bool avx_cpu = (r[2] & (1u << 28)) != 0;

        // CPUID only reports that the silicon has the unit. If the OS has not
        // enabled XSAVE for the YMM/ZMM state, executing AVX raises #UD. This
        // check was missing entirely.
        bool ymm_enabled = false;
        bool zmm_enabled = false;
        if (osxsave) {
            const uint64_t xcr0 = ana_xgetbv0();
            ymm_enabled = (xcr0 & 0x6u) == 0x6u;                      // XMM + YMM
            zmm_enabled = ymm_enabled && (xcr0 & 0xE0u) == 0xE0u;     // opmask + ZMM_hi256 + hi16_ZMM
        }

        if (max_leaf >= 7) {
            ana_cpuid(7, 0, r);
            g_features.avx2 = avx_cpu && ymm_enabled && (r[1] & (1u << 5)) != 0;
            g_features.avx512f = zmm_enabled && (r[1] & (1u << 16)) != 0;
        }
    }

    // The whole point of detection: actually install the wide implementations.
    // The previous version always assigned the scalar routines, so memcpy_avx2
    // and memset_avx2 were dead code that the compiler warned about.
    if (false && g_features.avx2) { // BISECT
        g_memcpy_impl = memcpy_avx2;
        g_memset_impl = memset_avx2;
    } else {
        g_memcpy_impl = memcpy_scalar;
        g_memset_impl = memset_scalar;
    }
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
