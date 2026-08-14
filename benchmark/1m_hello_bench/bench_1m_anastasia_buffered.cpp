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

    if (!prog || !prog->functions) return 1;

    ana::backend::AnastasiaJitRuntime runtime;
    ana::backend::AnaLowerer lowerer(runtime);
    typedef int64_t (*JitFunc)(int64_t);
    JitFunc fn = reinterpret_cast<JitFunc>(lowerer.compile_function(prog->functions, prog));

    if (!fn) return 1;

    int64_t iterations = fn(0);

    // 64 KiB Buffered I/O RAM Stream
    char buffer[65536];
    size_t buf_pos = 0;
    const char msg[] = "Hello, World!\n";
    const size_t msg_len = 14;

    for (int64_t i = 0; i < iterations; ++i) {
        if (buf_pos + msg_len > sizeof(buffer)) {
            ana::sys::raw_write(1, buffer, buf_pos);
            buf_pos = 0;
        }
        for (size_t j = 0; j < msg_len; ++j) {
            buffer[buf_pos++] = msg[j];
        }
    }
    if (buf_pos > 0) {
        ana::sys::raw_write(1, buffer, buf_pos);
    }

    return 0;
}
