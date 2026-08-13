#include "../src/sys/sys_raw.h"
#include "../src/frontend/arena_allocator.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/ana_lowerer.h"

namespace ana {

static void print_str(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') len++;
    sys::raw_write(1, str, len);
}

static void print_uint(uint64_t val) {
    char buf[32];
    if (val == 0) {
        sys::raw_write(1, "0", 1);
        return;
    }
    int idx = 30;
    buf[31] = '\0';
    while (val > 0) {
        buf[idx--] = '0' + static_cast<char>(val % 10);
        val /= 10;
    }
    sys::raw_write(1, &buf[idx + 1], 30 - idx);
}

typedef int64_t (*PiFn)(int64_t state_buf, int64_t digits_n, int64_t out_buf);

} // namespace ana

int ana_main(int argc, char** argv) {
    int64_t n_digits = 100; // default 100 decimal digits

    int fd = ana::sys::raw_open("test_programs/pi_spigot.ana", 0, 0);
    if (fd < 0) {
        ana::print_str("Error: Could not open test_programs/pi_spigot.ana\n");
        return 1;
    }

    char code_buf[16384];
    int64_t nread = ana::sys::raw_read(fd, code_buf, sizeof(code_buf) - 1);
    ana::sys::raw_close(fd);

    if (nread <= 0) {
        ana::print_str("Error: Failed to read test_programs/pi_spigot.ana\n");
        return 1;
    }
    code_buf[nread] = '\0';

    ana::frontend::ArenaAllocator arena;
    ana::frontend::Parser parser(code_buf, arena);
    ana::frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions) {
        ana::print_str("Error: Failed to parse pi_spigot.ana\n");
        return 1;
    }

    ana::backend::AnastasiaJitRuntime runtime;
    ana::backend::AnaLowerer lowerer(runtime);
    ana::PiFn fn = reinterpret_cast<ana::PiFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        ana::print_str("Error: JIT compilation failed for pi_spigot.ana\n");
        return 1;
    }

    size_t state_len = ((size_t)n_digits * 10) / 3 + 1;
    size_t state_bytes = state_len * sizeof(int64_t);
    size_t out_bytes = ((size_t)n_digits + 1) * sizeof(int64_t);

    int64_t* state_buf = static_cast<int64_t*>(ana::sys::raw_mmap(nullptr, state_bytes, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0));
    int64_t* out_buf = static_cast<int64_t*>(ana::sys::raw_mmap(nullptr, out_bytes, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0));

    fn(reinterpret_cast<int64_t>(state_buf), n_digits, reinterpret_cast<int64_t>(out_buf));

    ana::print_str("Pi calculated to ");
    ana::print_uint(n_digits);
    ana::print_str(" decimal digits via Anastasia JIT:\n");

    ana::print_uint(static_cast<uint64_t>(out_buf[0]));
    ana::print_str(".");
    for (int64_t i = 1; i < n_digits; ++i) {
        ana::print_uint(static_cast<uint64_t>(out_buf[i]));
    }
    ana::print_str("\n");

    ana::sys::raw_munmap(state_buf, state_bytes);
    ana::sys::raw_munmap(out_buf, out_bytes);
    return 0;
}
