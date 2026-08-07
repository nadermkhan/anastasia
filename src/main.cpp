#include "sys/sys_raw.h"
#include "sys/cpu_features.h"
#include "../tests/test_suite.h"
#include "../examples/example_runner.h"

int ana_main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    bool test_success = ana::tests::run_all_tests();
    bool example_success = ana::examples::run_all_examples();
    return (test_success && example_success) ? 0 : 1;
}
