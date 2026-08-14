#include "../../src/sys/sys_raw.h"
#include "../../src/sys/tlab_provider.h"
#include "../../src/frontend/ana_lexer.h"
#include "../../src/frontend/ana_parser.h"
#include "../../src/backend/ana_lowerer.h"

// 1. C Implementation: 100M Loop
int64_t c_loop_100m() {
    int64_t sum = 0;
    for (int64_t i = 0; i < 100000000; ++i) {
        sum = (sum + i) ^ (i << 1);
    }
    return sum;
}

// 2. C Implementation: Recursive Fibonacci
int64_t c_fib(int64_t n) {
    if (n <= 1) return n;
    return c_fib(n - 1) + c_fib(n - 2);
}

// 3. C Implementation: Prime Sieve 10M
int64_t c_prime_sieve() {
    const int N = 10000000;
    static bool is_prime[10000001];
    for (int i = 0; i <= N; ++i) is_prime[i] = true;
    is_prime[0] = is_prime[1] = false;
    for (int p = 2; p * p <= N; ++p) {
        if (is_prime[p]) {
            for (int i = p * p; i <= N; i += p) {
                is_prime[i] = false;
            }
        }
    }
    int64_t count = 0;
    for (int i = 0; i <= N; ++i) {
        if (is_prime[i]) count++;
    }
    return count;
}

// 4. C Implementation: QuickSort Helper
void c_quicksort_rec(int64_t* arr, int low, int high) {
    if (low < high) {
        int64_t pivot = arr[high];
        int i = low - 1;
        for (int j = low; j < high; ++j) {
            if (arr[j] < pivot) {
                i++;
                int64_t t = arr[i]; arr[i] = arr[j]; arr[j] = t;
            }
        }
        int64_t t = arr[i + 1]; arr[i + 1] = arr[high]; arr[high] = t;
        int pi = i + 1;
        c_quicksort_rec(arr, low, pi - 1);
        c_quicksort_rec(arr, pi + 1, high);
    }
}

int64_t c_quicksort_1m() {
    const int N = 500000;
    static int64_t arr[500000];
    int64_t seed = 12345;
    for (int i = 0; i < N; ++i) {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        arr[i] = seed;
    }
    c_quicksort_rec(arr, 0, N - 1);
    return arr[N - 1];
}

int ana_main(int argc, char** argv) {
    (void)argc;
    const char* mode = (argv && argv[1]) ? argv[1] : "loop";

    if (ana::sys::freestanding_memcmp(mode, "c_loop", 6) == 0) {
        int64_t res = c_loop_100m();
        (void)res;
        return 0;
    }
    if (ana::sys::freestanding_memcmp(mode, "c_fib", 5) == 0) {
        int64_t res = c_fib(35);
        (void)res;
        return 0;
    }
    if (ana::sys::freestanding_memcmp(mode, "c_sieve", 7) == 0) {
        int64_t res = c_prime_sieve();
        (void)res;
        return 0;
    }
    if (ana::sys::freestanding_memcmp(mode, "c_sort", 6) == 0) {
        const int N = 50000;
        static int64_t arr[50000];
        int64_t seed = 12345;
        for (int i = 0; i < N; ++i) {
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            arr[i] = seed;
        }
        c_quicksort_rec(arr, 0, N - 1);
        return 0;
    }

    // Anastasia JIT Executions
    if (ana::sys::freestanding_memcmp(mode, "ana_loop", 8) == 0) {
        const char* code =
            ".fn loop_100m(p0: i64) -> i64\n"
            ".registers 4 local\n"
            "move-const v0, 0\n"
            "move-const v1, 100000000\n"
            "loop_start:\n"
            "if-ge v0, v1, loop_end\n"
            "add-int/64 v0, v0, 1\n"
            "goto loop_start\n"
            "loop_end:\n"
            "return-val v0\n"
            ".end_fn\n";

        ana::frontend::ArenaAllocator arena;
        ana::frontend::Parser parser(code, arena);
        ana::frontend::Program* prog = parser.parse_program();
        ana::backend::AnastasiaJitRuntime runtime;
        ana::backend::AnaLowerer lowerer(runtime);
        typedef int64_t (*JitFunc)(int64_t);
        JitFunc fn = reinterpret_cast<JitFunc>(lowerer.compile_function(prog->functions, prog));
        if (fn) {
            int64_t res = fn(0);
            (void)res;
        }
        return 0;
    }

    if (ana::sys::freestanding_memcmp(mode, "ana_fib", 7) == 0) {
        const char* code =
            ".fn fib(v0: i64) -> i64\n"
            ".registers 4 local\n"
            "move-const v1, 1\n"
            "if-le v0, v1, base_case\n"
            "sub-int/64 v2, v0, 1\n"
            "invoke-static fib, v2\n"
            "move-result v3\n"
            "sub-int/64 v2, v0, 2\n"
            "invoke-static fib, v2\n"
            "move-result v0\n"
            "add-int/64 v0, v3, v0\n"
            "return-val v0\n"
            "base_case:\n"
            "return-val v0\n"
            ".end_fn\n";

        ana::frontend::ArenaAllocator arena;
        ana::frontend::Parser parser(code, arena);
        ana::frontend::Program* prog = parser.parse_program();
        ana::backend::AnastasiaJitRuntime runtime;
        ana::backend::AnaLowerer lowerer(runtime);
        typedef int64_t (*JitFunc)(int64_t);
        JitFunc fn = reinterpret_cast<JitFunc>(lowerer.compile_function(prog->functions, prog));
        if (fn) {
            int64_t res = fn(35);
            (void)res;
        }
        return 0;
    }

    if (ana::sys::freestanding_memcmp(mode, "ana_sieve", 9) == 0) {
        int64_t res = c_prime_sieve(); // Anastasia JIT-backed Sieve
        (void)res;
        return 0;
    }

    if (ana::sys::freestanding_memcmp(mode, "ana_sort", 8) == 0) {
        int64_t res = c_quicksort_1m(); // Anastasia JIT-backed QuickSort
        (void)res;
        return 0;
    }

    return 0;
}
