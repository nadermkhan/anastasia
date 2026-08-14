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



#include "debugger/ana_debugger.h"
#include "sys/ana_trap_handler.h"

int ana_main(int argc, char** argv) {
    ana::sys::init_trap_handler();

    if (argc >= 3 && streq_check(argv[1], "--debug")) {
        ana::debugger::AnaDebugger dbg;
        if (!dbg.load_program_from_file(argv[2])) {
            print_cli_msg("[AnaDebugger ERROR] Failed to load program from file: ");
            print_cli_msg(argv[2]);
            print_cli_msg("\n");
            return 1;
        }
        dbg.run_interactive_repl();
        return 0;
    }

    // `--aot` with too few arguments used to fall through and silently run the
    // whole test suite instead of reporting a usage error.
    if (argc >= 2 && streq_check(argv[1], "--aot") && argc < 4) {
        print_cli_msg("[AOT Compiler ERROR] Usage: anastasia_engine --aot <input.ana> <output.o>\n");
        return 2;
    }

    if (argc > 1 && streq_check(argv[1], "--debug-parse")) {
        int fd = ana::sys::raw_open(argv[2], 0, 0);
        char buf[8096];
        int64_t n = ana::sys::raw_read(fd, buf, sizeof(buf)-1);
        ana::sys::raw_close(fd);
        buf[n] = '\0';
        ana::frontend::ArenaAllocator arena;
        ana::frontend::Parser parser(buf, arena);
        ana::frontend::Program* prog = parser.parse_program();
        if (prog && prog->functions) {
            print_cli_msg("PARSE SUCCESS\n");
        } else {
            print_cli_msg("PARSE FAILURE: ");
            print_cli_msg(parser.error_message());
            print_cli_msg("\n");
        }
        return 0;
    }

    if (argc >= 4 && streq_check(argv[1], "--aot")) {
        const char* in_filepath = argv[2];
        const char* out_obj = argv[3];

        print_cli_msg("[AOT Compiler] Compiling Anastasia Assembly source '");
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

        // A single read into a fixed 64 KiB buffer silently truncated any
        // larger source mid-token and then compiled the fragment as if it were
        // the whole program. Read to end of file into a growing buffer.
        size_t cap = 65536;
        size_t len = 0;
        char* code_buf = static_cast<char*>(malloc(cap));
        if (!code_buf) {
            ana::sys::raw_close(fd);
            print_cli_msg("[AOT Compiler ERROR] Out of memory reading source\n");
            return 1;
        }

        for (;;) {
            if (len + 1 >= cap) {
                size_t new_cap = cap * 2;
                char* grown = static_cast<char*>(realloc(code_buf, new_cap));
                if (!grown) {
                    free(code_buf);
                    ana::sys::raw_close(fd);
                    print_cli_msg("[AOT Compiler ERROR] Out of memory reading source\n");
                    return 1;
                }
                code_buf = grown;
                cap = new_cap;
            }
            int64_t n = ana::sys::raw_read(fd, code_buf + len, cap - 1 - len);
            if (n < 0) {
                free(code_buf);
                ana::sys::raw_close(fd);
                print_cli_msg("[AOT Compiler ERROR] Read error on input source file\n");
                return 1;
            }
            if (n == 0) break;
            len += static_cast<size_t>(n);
        }
        ana::sys::raw_close(fd);

        if (len == 0) {
            free(code_buf);
            print_cli_msg("[AOT Compiler ERROR] Empty or unreadable source file\n");
            return 1;
        }
        code_buf[len] = '\0';

        ana::frontend::ArenaAllocator arena;
        ana::frontend::Parser parser(code_buf, arena);
        ana::frontend::Program* prog = parser.parse_program();

        if (!prog || !prog->functions) {
            // The parser can report a reason now, so surface it.
            print_cli_msg("[AOT Compiler ERROR] Failed to parse AST from input source: ");
            print_cli_msg(parser.error_message());
            print_cli_msg("\n");
            free(code_buf);
            return 1;
        }

        ana::backend::AnastasiaJitRuntime runtime;
        ana::backend::AnaLowerer lowerer(runtime);

        bool success = lowerer.compile_to_elf(prog, out_obj);
        // Freed only after lowering: AST nodes may still reference the source.
        if (success) {
            print_cli_msg("[AOT Compiler SUCCESS] Emitted relocatable ELF object file: ");
            print_cli_msg(out_obj);
            print_cli_msg("\n");
            free(code_buf);
            return 0;
        } else {
            print_cli_msg("[AOT Compiler ERROR] Failed to generate ELF object file\n");
            free(code_buf);
            return 1;
        }
    }

    bool test_success = ana::tests::run_all_tests();
    bool example_success = ana::examples::run_all_examples();
    return (test_success && example_success) ? 0 : 1;
}
