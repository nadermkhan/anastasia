#include "../../src/sys/sys_raw.h"
#include "../../src/sys/tlab_provider.h"
#include "../../src/frontend/ana_lexer.h"
#include "../../src/frontend/ana_parser.h"
#include "../../src/backend/ana_lowerer.h"

int ana_main(int argc, char** argv) {
    (void)argc; (void)argv;
    const char* smali_code =
        ".fn print_1m_hello(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v0, 0\n"
        "move-const v1, 1000000\n"
        "loop_start:\n"
        "if-ge v0, v1, loop_end\n"
        "add-int/64 v0, v0, 1\n"
        "goto loop_start\n"
        "loop_end:\n"
        "return-val v0\n"
        ".end_fn\n";

    ana::frontend::ArenaAllocator arena;
    ana::frontend::Parser parser(smali_code, arena);
    ana::frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions) {
        ana::sys::raw_write(2, "Parse failure!\n", 15);
        return 1;
    }

    ana::backend::AnastasiaJitRuntime runtime;
    ana::backend::AnaLowerer lowerer(runtime);
    typedef int64_t (*JitFunc)(int64_t);
    JitFunc fn = reinterpret_cast<JitFunc>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        ana::sys::raw_write(2, "JIT compilation failure!\n", 25);
        return 1;
    }

    // Run Anastasia JIT Compiled function
    int64_t iterations = fn(0);

    // Stream 1,000,000 "Hello, World!\n" buffers freestanding
    const char msg[] = "Hello, World!\n";
    for (int64_t i = 0; i < iterations; ++i) {
        ana::sys::raw_write(1, msg, 14);
    }

    return 0;
}
