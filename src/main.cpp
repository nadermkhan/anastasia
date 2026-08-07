#include "sys/sys_raw.h"
#include "sys/cpu_features.h"
#include "frontend/ana_lexer.h"
#include "frontend/ana_parser.h"
#include "backend/ana_lowerer.h"
#include "backend/vmem_provider.h"
#include "../tests/test_suite.h"
#include "../examples/example_runner.h"

static bool streq_check(const char* s1, const char* s2) {
    if (!s1 || !s2) return false;
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == '\0' && s2[i] == '\0';
}

static void print_cli_msg(const char* s) {
    if (!s) return;
    size_t len = 0;
    while (s[len] != '\0') len++;
    ana::sys::raw_write(1, s, len);
}



int ana_main(int argc, char** argv) {
    if (argc >= 4 && streq_check(argv[1], "--aot")) {
        const char* in_filepath = argv[2];
        const char* out_obj = argv[3];

        print_cli_msg("[AOT Compiler] Compiling Extended Smali source '");
        print_cli_msg(in_filepath);
        print_cli_msg("' -> '");
        print_cli_msg(out_obj);
        print_cli_msg("'\n");

        int fd = ana::sys::raw_open(in_filepath, 0 /* O_RDONLY */, 0);
        if (fd < 0) {
            print_cli_msg("[AOT Compiler ERROR] Cannot open input source file: ");
            print_cli_msg(in_filepath);
            print_cli_msg("\n");
            return 1;
        }

        static char code_buf[65536];
        ana::sys::freestanding_memset(code_buf, 0, sizeof(code_buf));
        int64_t bytes = ana::sys::raw_read(fd, code_buf, sizeof(code_buf) - 1);
        ana::sys::raw_close(fd);

        if (bytes <= 0) {
            print_cli_msg("[AOT Compiler ERROR] Empty or unreadable source file\n");
            return 1;
        }
        code_buf[bytes] = '\0';

        ana::frontend::ArenaAllocator arena;
        ana::frontend::Parser parser(code_buf, arena);
        ana::frontend::Program* prog = parser.parse_program();

        if (!prog || !prog->functions) {
            print_cli_msg("[AOT Compiler ERROR] Failed to parse AST from input source\n");
            return 1;
        }

        ana::backend::AnastasiaJitRuntime runtime;
        ana::backend::AnaLowerer lowerer(runtime);

        bool success = lowerer.compile_to_elf(prog, out_obj);
        if (success) {
            print_cli_msg("[AOT Compiler SUCCESS] Emitted relocatable ELF object file: ");
            print_cli_msg(out_obj);
            print_cli_msg("\n");
            return 0;
        } else {
            print_cli_msg("[AOT Compiler ERROR] Failed to generate ELF object file\n");
            return 1;
        }
    }

    bool test_success = ana::tests::run_all_tests();
    bool example_success = ana::examples::run_all_examples();
    return (test_success && example_success) ? 0 : 1;
}
