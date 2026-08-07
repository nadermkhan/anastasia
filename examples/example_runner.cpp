#include "example_runner.h"
#include "../src/sys/sys_raw.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/ana_lowerer.h"
#include "../src/backend/inline_cache.h"

namespace ana {
namespace examples {

static void print(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') len++;
    sys::raw_write(1, str, len);
}

static void print_int(int64_t val) {
    char buf[32];
    if (val == 0) {
        sys::raw_write(1, "0", 1);
        return;
    }
    bool neg = false;
    if (val < 0) {
        neg = true;
        val = -val;
    }
    int idx = 30;
    buf[31] = '\0';
    while (val > 0) {
        buf[idx--] = '0' + static_cast<char>(val % 10);
        val /= 10;
    }
    if (neg) buf[idx--] = '-';
    sys::raw_write(1, &buf[idx + 1], 30 - idx);
}

static bool run_example_1() {
    print("\n--- Running Example 1: 01_math_basics.ana ---\n");
    const char* code =
        ".fn math_demo(p0: i64, p1: i64) -> i64\n"
        ".registers 2 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v1, v0, 500\n"
        "return-val v1\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*MathFn)(int64_t, int64_t);
    MathFn fn = reinterpret_cast<MathFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 1\n");
        return false;
    }

    int64_t result = fn(1000, 250);
    print("Input: p0 = 1000, p1 = 250\n");
    print("Execution Output: (1000 + 250) - 500 = ");
    print_int(result);
    print("\n");

    if (result == 750) {
        print("[SUCCESS] Example 1 passed cleanly!\n");
        return true;
    } else {
        print("[FAIL] Unexpected output for Example 1\n");
        return false;
    }
}

static bool run_example_2() {
    print("\n--- Running Example 2: 02_vtable_dispatch.ana ---\n");
    const char* code =
        ".fn vtable_demo(p0: ptr) -> i64\n"
        ".registers 1 local\n"
        "call-virt-fast p0, 0 -> v0\n"
        "return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*VTableFn)(void*);
    VTableFn fn = reinterpret_cast<VTableFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 2\n");
        return false;
    }

    void* mock_vtable[2];
    auto mock_method = [](void* obj) -> int64_t {
        (void)obj;
        return 999;
    };
    mock_vtable[0] = reinterpret_cast<void*>(+mock_method);

    struct MockObj {
        void** vtable;
    } obj;
    obj.vtable = mock_vtable;

    int64_t result = fn(&obj);
    print("Execution Output: Virtual method dispatch returned ");
    print_int(result);
    print("\n");

    if (result == 999) {
        print("[SUCCESS] Example 2 passed cleanly!\n");
        return true;
    } else {
        print("[FAIL] Unexpected output for Example 2\n");
        return false;
    }
}

static bool run_example_3() {
    print("\n--- Running Example 3: 03_memory_struct.ana ---\n");
    const char* code =
        ".fn memory_demo(p0: ptr, p1: i64) -> i64\n"
        ".registers 1 local\n"
        "store-mem [p0 + 8], p1\n"
        "load-mem v0, [p0 + 8]\n"
        "return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*MemFn)(void*, int64_t);
    MemFn fn = reinterpret_cast<MemFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 3\n");
        return false;
    }

    struct MockStruct {
        int64_t header;
        int64_t payload;
    };
    MockStruct* s = static_cast<MockStruct*>(sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0));
    s->header = 0;
    s->payload = 0;

    int64_t result = fn(s, 4242);
    print("Execution Output: Stored and reloaded offset [p0 + 8] = ");
    print_int(result);
    print("\n");

    if (result == 4242 && s->payload == 4242) {
        sys::raw_munmap(s, 4096);
        print("[SUCCESS] Example 3 passed cleanly!\n");
        return true;
    } else {
        sys::raw_munmap(s, 4096);
        print("[FAIL] Unexpected output for Example 3\n");
        return false;
    }
}

static bool run_example_4() {
    print("\n--- Running Example 4: 04_constant_folding_dce.ana ---\n");
    const char* code =
        ".fn folding_demo(p0: i64) -> i64\n"
        ".registers 2 local\n"
        "add-int/64 v0, 100, 200\n"
        "add-int/64 v1, p0, v0\n"
        "return-val v1\n"
        "add-int/64 v0, v0, 999\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*FoldFn)(int64_t);
    FoldFn fn = reinterpret_cast<FoldFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 4\n");
        return false;
    }

    int64_t result = fn(50);
    print("Input: p0 = 50\n");
    print("Execution Output: 50 + (100 + 200 [folded]) = ");
    print_int(result);
    print("\n");

    if (result == 350) {
        print("[SUCCESS] Example 4 passed cleanly!\n");
        return true;
    } else {
        print("[FAIL] Unexpected output for Example 4\n");
        return false;
    }
}

static bool run_example_5() {
    print("\n--- Running Example 5: 05_control_flow_loop.ana ---\n");
    const char* code =
        ".fn loop_demo(p0: i64) -> i64\n"
        ".registers 2 local\n"
        "move-const v0, 0\n"
        "move-const v1, 0\n"
        "loop_start:\n"
        "if-ge v1, p0, .loop_end\n"
        "add-int/64 v0, v0, v1\n"
        "add-int/64 v1, v1, 1\n"
        "goto .loop_start\n"
        "loop_end:\n"
        "return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*LoopFn)(int64_t);
    LoopFn fn = reinterpret_cast<LoopFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 5\n");
        return false;
    }

    int64_t result = fn(10); // Sum 0..9 = 45
    print("Input: p0 = 10 (Loop iteration 0 to 9)\n");
    print("Execution Output: Loop summation = ");
    print_int(result);
    print("\n");

    if (result == 45) {
        print("[SUCCESS] Example 5 passed cleanly!\n");
        return true;
    } else {
        print("[FAIL] Unexpected output for Example 5\n");
        return false;
    }
}

static bool run_example_6() {
    print("\n--- Running Example 6: 06_bitwise_ops.ana ---\n");
    const char* code =
        ".fn bitwise_demo(p0: i64, p1: i64) -> i64\n"
        ".registers 2 local\n"
        "and-int/64 v0, p0, 255\n"
        "shl-int/64 v1, v0, p1\n"
        "return-val v1\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*BitFn)(int64_t, int64_t);
    BitFn fn = reinterpret_cast<BitFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 6\n");
        return false;
    }

    int64_t result = fn(0x1234, 4); // (0x1234 & 255) << 4 = 0x34 << 4 = 0x340 = 832
    print("Input: p0 = 0x1234, p1 = 4\n");
    print("Execution Output: (0x1234 & 255) << 4 = ");
    print_int(result);
    print("\n");

    if (result == 832) {
        print("[SUCCESS] Example 6 passed cleanly!\n");
        return true;
    } else {
        print("[FAIL] Unexpected output for Example 6\n");
        return false;
    }
}

static bool run_example_7() {
    print("\n--- Running Example 7: 07_hardware_atomics.ana ---\n");
    const char* code =
        ".fn atomic_demo(p0: ptr, p1: i64) -> i64\n"
        ".registers 1 local\n"
        "atomic-add/64 [p0 + 0], p1\n"
        "fence\n"
        "load-mem v0, [p0 + 0]\n"
        "return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*AtomicFn)(void*, int64_t);
    AtomicFn fn = reinterpret_cast<AtomicFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 7\n");
        return false;
    }

    int64_t target_mem = 100;
    int64_t result = fn(&target_mem, 50);
    print("Input: initial target_mem = 100, add = 50\n");
    print("Execution Output: Lock-free atomic add & fence result = ");
    print_int(result);
    print("\n");

    if (result == 150 && target_mem == 150) {
        print("[SUCCESS] Example 7 passed cleanly!\n");
        return true;
    } else {
        print("[FAIL] Unexpected output for Example 7\n");
        return false;
    }
}

static bool run_example_8() {
    print("\n--- Running Example 8: 08_object_instantiation.ana ---\n");
    const char* code =
        ".class Widget\n"
        ".end_class\n"
        ".fn main(p0: i64) -> i64\n"
        ".registers 2 local\n"
        "new-instance v0, Widget\n"
        "move-const v1, 777\n"
        "store-mem [v0 + 16], v1\n"
        "load-mem v1, [v0 + 16]\n"
        "add-int/64 v0, v1, p0\n"
        "return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*ObjMainFn)(int64_t);
    ObjMainFn fn = reinterpret_cast<ObjMainFn>(lowerer.compile_function(prog->functions, prog));

    if (!fn) {
        print("[FAIL] Compilation failed for Example 8\n");
        return false;
    }

    int64_t result = fn(23);
    print("Input: p0 = 23 (Instantiate Widget, store field = 777, return field + p0)\n");
    print("Execution Output: Object instantiation & field add result = ");
    print_int(result);
    print("\n");

    if (result == 800) {
        print("[SUCCESS] Example 8 passed cleanly!\n");
        return true;
    } else {
        print("[FAIL] Unexpected output for Example 8\n");
        return false;
    }
}

bool run_all_examples() {
    print("\n=======================================================\n");
    print("    Anastasia Extended Smali Example Execution Suite\n");
    print("=======================================================\n");

    bool all_ok = true;
    all_ok &= run_example_1();
    all_ok &= run_example_2();
    all_ok &= run_example_3();
    all_ok &= run_example_4();
    all_ok &= run_example_5();
    all_ok &= run_example_6();
    all_ok &= run_example_7();
    all_ok &= run_example_8();

    print("\n=======================================================\n");
    if (all_ok) {
        print("    ALL EXAMPLE PROGRAMS EXECUTED SUCCESSFULLY!\n");
    } else {
        print("    SOME EXAMPLES FAILED!\n");
    }
    print("=======================================================\n\n");
    return all_ok;
}

} // namespace examples
} // namespace ana
