#ifndef ANA_CPU_FEATURES_H
#define ANA_CPU_FEATURES_H

#include <stddef.h>
#include <stdint.h>

namespace ana {
namespace sys {

struct CpuFeatures {
    bool avx512f;
    bool avx2;
    bool sse2;
    bool neon;
};

void detect_cpu_features();
const CpuFeatures& get_cpu_features();

// Typedefs for SIMD routed memory operations
typedef void* (*memcpy_fn_t)(void*, const void*, size_t);
typedef void* (*memset_fn_t)(void*, int, size_t);

extern memcpy_fn_t g_memcpy_impl;
extern memset_fn_t g_memset_impl;

} // namespace sys
} // namespace ana

#endif // ANA_CPU_FEATURES_H
