#ifndef ANA_BENCH_SUITE_H
#define ANA_BENCH_SUITE_H

#include "../src/sys/sys_raw.h"
#include "../src/frontend/arena_allocator.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/ana_lowerer.h"
#include "../src/backend/vmem_provider.h"
#include "../src/sys/object_heap.h"

namespace ana {
namespace benchmark {

struct BenchResult {
    const char* name;
    uint64_t iterations;
    uint64_t total_ns;
    uint64_t total_cycles;
    double ops_per_sec;
    double ns_per_op;
};

void run_all_benchmarks();

} // namespace benchmark
} // namespace ana

#endif // ANA_BENCH_SUITE_H
