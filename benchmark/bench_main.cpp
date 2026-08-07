#include "bench_suite.h"

int ana_main(int argc, char** argv) {
    (void)argc; (void)argv;
    ana::benchmark::run_all_benchmarks();
    return 0;
}
