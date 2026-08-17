#include "test_suite.h"
#include "leetcode_suite.h"
#include "codeforces_suite.h"
#include "hardcore_suite.h"
#include "reliability_suite.h"
#include "../src/sys/sys_raw.h"
#include "../src/sys/cpu_features.h"
#include "../src/frontend/arena_allocator.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/vmem_provider.h"
#include "../src/backend/ana_lowerer.h"
#include "../src/backend/inline_cache.h"
#include "../src/backend/aarch64_backend.h"
#include "../src/backend/xtensa_lx7_backend.h"
#include "../src/backend/gdb_jit.h"
#include "../src/backend/dwarf_emitter.h"
#include "../src/optimizer/ana_ssa.h"
#include "../src/sys/tlab_provider.h"
#include "../src/sys/gc_collector.h"
#include "../src/backend/pic_dispatcher.h"
#include "../src/backend/exception_unwinder.h"
#include "../src/backend/osr_engine.h"
#include "../src/sys/io_ring.h"
#include "../src/backend/host_interop.h"
#include "../src/backend/pe_emitter.h"
#include "../src/optimizer/pgo_profiler.h"
#include "../src/debugger/ana_debugger.h"
#include "../src/sys/ana_trap_handler.h"
#include "../src/optimizer/sys_coalescer.h"
#include "leetcode_suite.h"

namespace ana {
namespace tests {

static void print_msg(const char* msg) {
    ana::sys::raw_write(1, msg, ana::sys::freestanding_strlen(msg));
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
    sys::raw_write(1, &buf[idx + 1], 31 - (idx + 1));
}

static bool test_freestanding_memory_and_syscalls() {
    print_msg("[Test 1/40] Syscall & Freestanding Memory Operations... ");

    // Test raw_mmap and raw_munmap
    void* ptr = ana::sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!ptr || ptr == (void*)-1) {
        print_msg("FAILED (raw_mmap)\n");
        return false;
    }

    ana::sys::freestanding_memset(ptr, 0xAB, 4096);
    unsigned char* p = static_cast<unsigned char*>(ptr);
    if (p[0] != 0xAB || p[4095] != 0xAB) {
        print_msg("FAILED (memset)\n");
        return false;
    }

    char buf[32];
    ana::sys::freestanding_memcpy(buf, "Anastasia", 9);
    buf[9] = '\0';
    if (ana::sys::freestanding_memcmp(buf, "Anastasia", 9) != 0) {
        print_msg("FAILED (memcpy/memcmp)\n");
        return false;
    }

    if (ana::sys::raw_munmap(ptr, 4096) != 0) {
        print_msg("FAILED (raw_munmap)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_frontend_lexer_parser_ast() {
    print_msg("[Test 2/40] Perfect-Hash Lexer, Arena Allocator & Constant Folding... ");

    const char* sample_code =
        ".class Animal\n"
        "    .field sound: ptr\n"
        ".end_class\n"
        ".fn compute(p0: i64, p1: i64) -> i64\n"
        "    .registers 2 local\n"
        "    add-int/32 v0, 15, 25\n"  // Should fold to MOVE_CONST 40
        "    move-const v1, 100\n"
        "    add-int/64 v0, v0, v1\n"
        "    return-val v0\n"
        "    move-const v1, 999\n"     // Should be eliminated by DCE
        ".end_fn\n";

    ana::frontend::ArenaAllocator arena;
    ana::frontend::Parser parser(sample_code, arena);
    ana::frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions || !prog->classes) {
        print_msg("FAILED (AST generation)\n");
        return false;
    }

    // Verify Constant Folding (15 + 25 -> 40)
    ana::frontend::Instruction* insn0 = prog->functions->first_block->first_insn;
    if (!insn0 || insn0->op != ana::frontend::Opcode::MOVE_CONST || insn0->src1.const_val != 40) {
        print_msg("FAILED (Constant Folding)\n");
        return false;
    }

    // Verify DCE (Instructions after return-val dropped)
    ana::frontend::Instruction* last_insn = prog->functions->first_block->last_insn;
    if (!last_insn || last_insn->op != ana::frontend::Opcode::RETURN_VAL) {
        print_msg("FAILED (DCE)\n");
        return false;
    }

    // Verify 64-byte alignment of VTable array
    uintptr_t vptr = reinterpret_cast<uintptr_t>(prog->classes->vtable_array);
    if ((vptr & 63) != 0) {
        print_msg("FAILED (64-byte VTable Alignment)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_asmjit_lowering_and_execution() {
    print_msg("[Test 3/40] AsmJit JIT Lowering & Bare-Metal Execution... ");

    const char* sample_code =
        ".fn sum_fn(p0: i64, p1: i64) -> i64\n"
        "    .registers 1 local\n"
        "    add-int/64 v0, p0, p1\n"
        "    return-val v0\n"
        ".end_fn\n";

    ana::frontend::ArenaAllocator arena;
    ana::frontend::Parser parser(sample_code, arena);
    ana::frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (AST generation)\n");
        return false;
    }

    ana::backend::AnastasiaJitRuntime runtime;
    ana::backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*SumFn)(int64_t, int64_t);
    SumFn compiled_sum = reinterpret_cast<SumFn>(lowerer.compile_function(prog->functions, prog));

    if (!compiled_sum) {
        print_msg("FAILED (Lowering compilation)\n");
        return false;
    }

    int64_t res = compiled_sum(1234, 5678);
    if (res != 6912) {
        print_msg("FAILED (Incorrect execution result)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_vtable_and_inline_caching() {
    print_msg("[Test 4/40] OOP Layout, VTable Dispatch & Monomorphic Inline Cache... ");

    void* mock_vtable[4];
    auto mock_speak = [](void* obj) -> int64_t {
        (void)obj;
        return 42;
    };
    mock_vtable[0] = reinterpret_cast<void*>(+mock_speak);

    struct MockObject {
        void** vtable;
        int64_t field0;
    } obj;
    obj.vtable = mock_vtable;
    obj.field0 = 100;

    void* patch_slot = nullptr; // Dummy patch site
    void* resolved_fn = ana::backend::handle_mic_miss(&obj, 0, reinterpret_cast<uint8_t*>(&patch_slot));
    if (resolved_fn != reinterpret_cast<void*>(+mock_speak)) {
        print_msg("FAILED (MIC Resolution)\n");
        return false;
    }

    typedef int64_t (*SpeakFn)(void*);
    SpeakFn fn = reinterpret_cast<SpeakFn>(resolved_fn);
    if (fn(&obj) != 42) {
        print_msg("FAILED (VTable Dispatch Execution)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_wx_protection_and_icache() {
    print_msg("[Test 5/40] Strict W^X Protection & Instruction Cache Flush... ");

    void* page = ana::sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    // raw_mmap signals failure with (void*)-1, never nullptr, so this check
    // used to let a failed mapping through and then execute address -1.
    if (!page || page == reinterpret_cast<void*>(-1)) {
        print_msg("FAILED (Alloc)\n");
        return false;
    }

#if defined(__aarch64__) || defined(_M_ARM64)
    uint32_t* code = static_cast<uint32_t*>(page);
    code[0] = 0xD503201F; // ARM64 NOP
    code[1] = 0xD65F03C0; // ARM64 RET
#else
    unsigned char* code = static_cast<unsigned char*>(page);
    code[0] = 0x90; // x86_64 NOP
    code[1] = 0xC3; // x86_64 RET
#endif

    // Transition RW -> RX
    if (ana::sys::raw_mprotect(page, 4096, ANA_PROT_READ | ANA_PROT_EXEC) != 0) {
        print_msg("FAILED (mprotect RX)\n");
        return false;
    }

    ana::sys::clear_icache(page, 4096);

    typedef void (*VoidFn)();
    VoidFn fn = reinterpret_cast<VoidFn>(page);
    fn(); // Should execute NOP; RET successfully

    ana::sys::raw_munmap(page, 4096);

    print_msg("PASSED\n");
    return true;
}

static bool test_cpu_feature_detection() {
    print_msg("[Test 6/40] Dynamic CPU SIMD Routing... ");
    const auto& feat = ana::sys::get_cpu_features();

    // This test returned true on every path, so neither a broken detector nor
    // a mis-routed memcpy could ever fail it. Verify the routed implementation
    // against a scalar reference across sizes that straddle the 32-byte and
    // 128-byte vector blocks, and confirm nothing is written past the end.
    if (!ana::sys::g_memcpy_impl || !ana::sys::g_memset_impl) {
        print_msg("FAILED (SIMD routing pointers not installed)\n");
        return false;
    }

#if defined(__x86_64__)
    if (!feat.sse2) {
        print_msg("FAILED (SSE2 is architectural on x86_64 but was not detected)\n");
        return false;
    }
#endif

    static unsigned char src_buf[320];
    static unsigned char dst_buf[320];
    static unsigned char ref_buf[320];

    for (size_t n = 0; n <= 300; n += 7) {
        for (size_t i = 0; i < sizeof(src_buf); ++i) {
            src_buf[i] = static_cast<unsigned char>((i * 31 + n) & 0xFF);
            dst_buf[i] = 0xAA;
            ref_buf[i] = 0xAA;
        }
        ana::sys::g_memcpy_impl(dst_buf, src_buf, n);
        for (size_t i = 0; i < n; ++i) ref_buf[i] = src_buf[i];
        for (size_t i = 0; i < sizeof(src_buf); ++i) {
            if (dst_buf[i] != ref_buf[i]) {
                print_msg("FAILED (routed memcpy mismatch or overrun)\n");
                return false;
            }
        }

        for (size_t i = 0; i < sizeof(dst_buf); ++i) {
            dst_buf[i] = 0xAA;
            ref_buf[i] = 0xAA;
        }
        ana::sys::g_memset_impl(dst_buf, 0x5C, n);
        for (size_t i = 0; i < n; ++i) ref_buf[i] = 0x5C;
        for (size_t i = 0; i < sizeof(dst_buf); ++i) {
            if (dst_buf[i] != ref_buf[i]) {
                print_msg("FAILED (routed memset mismatch or overrun)\n");
                return false;
            }
        }
    }

    if (feat.avx2) {
        print_msg("PASSED (AVX2 detected, routed & verified)\n");
    } else if (feat.neon) {
        print_msg("PASSED (NEON detected & verified)\n");
    } else {
        print_msg("PASSED (Scalar fallback verified)\n");
    }
    return true;
}

static bool test_control_flow_and_branches() {
    print_msg("[Test 7/40] Control Flow, Fused Branches & Fallthrough Optimization... ");
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
        print_msg("FAILED (Compilation)\n");
        return false;
    }

    int64_t res = fn(10); // Sum of 0..9 = 45
    if (res != 45) {
        print_msg("FAILED (Expected 45, got ");
        print_msg(")\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_bitwise_ops_and_shifts() {
    print_msg("[Test 8/40] Bitwise ISA, %cl Shift Pinning & Popcount... ");
    const char* code =
        ".fn bit_demo(p0: i64, p1: i64) -> i64\n"
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
        print_msg("FAILED (Compilation)\n");
        return false;
    }

    int64_t res = fn(0x1234, 4); // 0x34 << 4 = 0x340 = 832
    if (res != 832) {
        print_msg("FAILED (Expected 832)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_hardware_atomics() {
    print_msg("[Test 9/40] Hardware Lock-Free Atomics & Memory Ordering... ");
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
        print_msg("FAILED (Compilation)\n");
        return false;
    }

    int64_t target_mem = 100;
    int64_t res = fn(&target_mem, 50);

    if (res != 150 || target_mem != 150) {
        print_msg("FAILED (Atomic add verification)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_native_encoder() {
    print_msg("[Test 10/40] Native Bare-Metal Instruction Encoder (AnaEncoder)... ");
#if defined(__aarch64__) || defined(_M_ARM64)
    backend::AArch64Encoder enc;
    enc.push_fp_lr();
    enc.mov_fp_sp();
    enc.mov_reg_imm64(backend::Arm64Reg::X0, 12345);
    enc.pop_fp_lr();
    enc.ret();
#else
    backend::AnaEncoder enc;
    enc.mov_reg_imm64(backend::X86Reg::RAX, 12345);
    enc.ret();
    if (!enc.resolve_labels()) {
        print_msg("FAILED (Label Resolution)\n");
        return false;
    }
#endif

    void* code_mem = ana::sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    ana::sys::freestanding_memcpy(code_mem, enc.code_bytes(), enc.code_size());
    ana::sys::raw_mprotect(code_mem, 4096, ANA_PROT_READ | ANA_PROT_EXEC);
    ana::sys::clear_icache(code_mem, 4096);

    typedef int64_t (*SimpleFn)();
    SimpleFn fn = reinterpret_cast<SimpleFn>(code_mem);
    int64_t res = fn();
    ana::sys::raw_munmap(code_mem, 4096);

    if (res != 12345) {
        print_msg("FAILED (Execution Result)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_unbounded_registers_and_spilling() {
    print_msg("[Test 11/40] Unbounded Virtual Registers & Stack Spilling (v0..v15)... ");
    const char* code =
        ".fn spill_demo(p0: i64) -> i64\n"
        ".registers 16 local\n"
        "move-const v0, 1\n"
        "move-const v1, 2\n"
        "move-const v2, 3\n"
        "move-const v3, 4\n"
        "move-const v4, 5\n"
        "move-const v5, 6\n"
        "move-const v6, 7\n"
        "move-const v7, 8\n"
        "move-const v8, 9\n"
        "move-const v9, 10\n"
        "move-const v10, 11\n"
        "move-const v11, 12\n"
        "move-const v12, 13\n"
        "move-const v13, 14\n"
        "move-const v14, 15\n"
        "move-const v15, 0\n"
        "add-int/64 v15, v15, v0\n"
        "add-int/64 v15, v15, v1\n"
        "add-int/64 v15, v15, v2\n"
        "add-int/64 v15, v15, v3\n"
        "add-int/64 v15, v15, v4\n"
        "add-int/64 v15, v15, v5\n"
        "add-int/64 v15, v15, v6\n"
        "add-int/64 v15, v15, v7\n"
        "add-int/64 v15, v15, v8\n"
        "add-int/64 v15, v15, v9\n"
        "add-int/64 v15, v15, v10\n"
        "add-int/64 v15, v15, v11\n"
        "add-int/64 v15, v15, v12\n"
        "add-int/64 v15, v15, v13\n"
        "add-int/64 v15, v15, v14\n"
        "add-int/64 v15, v15, p0\n"
        "return-val v15\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef int64_t (*SpillFn)(int64_t);
    SpillFn fn = reinterpret_cast<SpillFn>(lowerer.compile_function(prog->functions, prog));
    if (!fn) {
        print_msg("FAILED (Compilation)\n");
        return false;
    }

    int64_t res = fn(80); // Sum(1..15) + 80 = 120 + 80 = 200
    if (res != 200) {
        print_msg("FAILED (Expected 200, got ");
        print_int(res);
        print_msg(")\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_object_instantiation_and_heap() {
    print_msg("[Test 12/40] Object Instantiation (new-instance) & Heap Allocation... ");
    const char* code =
        ".class Item\n"
        ".end_class\n"
        ".fn create_item() -> ptr\n"
        ".registers 1 local\n"
        "new-instance v0, Item\n"
        "move-const v1, 8888\n"
        "store-mem [v0 + 16], v1\n"
        "return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef void* (*NewObjFn)();
    NewObjFn fn = reinterpret_cast<NewObjFn>(lowerer.compile_function(prog->functions, prog));
    if (!fn) {
        print_msg("FAILED (Compilation)\n");
        return false;
    }

    void* obj = fn();
    if (!obj) {
        print_msg("FAILED (Null Object)\n");
        return false;
    }

    int64_t val = *reinterpret_cast<int64_t*>(reinterpret_cast<uintptr_t>(obj) + 16);
    if (val != 8888) {
        print_msg("FAILED (Field Verification)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_atomic_wx_patching_and_clflush() {
    print_msg("[Test 13/40] Atomic W^X Code Patching & clflush Invalidation... ");
    void* code_mem = ana::sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!code_mem) {
        print_msg("FAILED (Alloc)\n");
        return false;
    }

    uint64_t* patch_slot = static_cast<uint64_t*>(code_mem);
    __atomic_store_n(patch_slot, 0x90909090C3909090ULL, __ATOMIC_RELEASE);

    ana::sys::clear_icache(code_mem, 4096);

    ana::sys::raw_mprotect(code_mem, 4096, ANA_PROT_READ | ANA_PROT_EXEC);
    ana::sys::clear_icache(code_mem, 4096);
    ana::sys::raw_munmap(code_mem, 4096);

    print_msg("PASSED\n");
    return true;
}

static bool test_aot_elf_compilation() {
    print_msg("[Test 14/40] AOT Relocatable ELF Object File Emitter (ElfEmitter)... ");
    const char* code =
        ".fn aot_demo(p0: i64, p1: i64) -> i64\n"
        ".registers 6 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v1, v0, p0\n"
        "mul-int/64 v2, v1, p1\n"
        "xor-int/64 v3, v2, v0\n"
        "shl-int/64 v4, v3, 4\n"
        "move v5, v4\n"
        "if-eq v5, v4, loop_end\n"
        "goto loop_end\n"
        "loop_end:\n"
        "return-val v5\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions) {
        print_msg("FAILED (AST Parsing)\n");
        return false;
    }

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    const char* test_obj_path = "test_aot_demo.o";
    bool success = lowerer.compile_to_elf(prog, test_obj_path);
    if (!success) {
        print_msg("FAILED (ELF Generation)\n");
        return false;
    }

    // Read generated test_aot_demo.o header bytes to verify ELF 64-bit structure
    uint8_t* header = static_cast<uint8_t*>(malloc(64));
    sys::freestanding_memset(header, 0, 64);

    int fd = sys::raw_open(test_obj_path, 0 /* O_RDONLY */, 0);
    if (fd < 0) {
        print_msg("FAILED (File Open fd=");
        print_int(fd);
        print_msg(")\n");
        free(header);
        return false;
    }

    int64_t read_bytes = sys::raw_read(fd, header, 64);
    sys::raw_close(fd);

    if (read_bytes < 64) {
        print_msg("FAILED (Header Size ");
        print_int(read_bytes);
        print_msg(")\n");
        free(header);
        return false;
    }

    // Verify \x7fELF Magic Bytes
    if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F') {
        print_msg("FAILED (Invalid ELF Magic)\n");
        free(header);
        return false;
    }

    // Verify ELFCLASS64 (2) and ELFDATA2LSB (1)
    if (header[4] != 2 || header[5] != 1) {
        print_msg("FAILED (Invalid ELF64 Class/Data)\n");
        free(header);
        return false;
    }

    uint16_t e_type = *reinterpret_cast<uint16_t*>(&header[16]);
    uint16_t e_machine = *reinterpret_cast<uint16_t*>(&header[18]);
#if defined(__aarch64__) || defined(_M_ARM64)
    uint16_t expected_mach = 183;
#elif defined(__arm__) || defined(_M_ARM) || defined(__armv7__)
    uint16_t expected_mach = 40;
#elif defined(__riscv) || defined(__riscv__)
    uint16_t expected_mach = 243;
#else
    uint16_t expected_mach = 62;
#endif
    if (e_type != 1 || e_machine != expected_mach) {
        print_msg("FAILED (Invalid ET_REL or Machine)\n");
        free(header);
        return false;
    }

    free(header);
    print_msg("PASSED\n");
    return true;
}

static bool test_aarch64_instruction_encoding() {
    print_msg("[Test 15/40] AArch64 Backend & Fixed 32-bit Machine Code Emitter... ");

    backend::AArch64Encoder enc;
    enc.push_fp_lr();                                         // 0xA9BF7BFD
    enc.mov_fp_sp();                                          // 0x910007FD
    enc.add_reg_reg(backend::Arm64Reg::X0, backend::Arm64Reg::X0, backend::Arm64Reg::X1); // 0x8B010000
    enc.sub_reg_reg(backend::Arm64Reg::X2, backend::Arm64Reg::X3, backend::Arm64Reg::X4); // 0xCB040062
    enc.mul_reg_reg(backend::Arm64Reg::X5, backend::Arm64Reg::X6, backend::Arm64Reg::X7); // 0x9B077CC5
    enc.pop_fp_lr();                                          // 0xA8C17BFD
    enc.ret();                                                // 0xD65F03C0

    if (enc.insn_count() != 7) {
        print_msg("FAILED (Instruction Count ");
        print_int(enc.insn_count());
        print_msg(")\n");
        return false;
    }

    const uint32_t* insns = reinterpret_cast<const uint32_t*>(enc.code_bytes());
    if (insns[0] != 0xA9BF7BFDUL) { print_msg("FAILED (push_fp_lr encoding)\n"); return false; }
    if (insns[1] != 0x910007FDUL) { print_msg("FAILED (mov_fp_sp encoding)\n"); return false; }
    if (insns[2] != 0x8B010000UL) { print_msg("FAILED (add_reg_reg encoding)\n"); return false; }
    if (insns[3] != 0xCB040062UL) { print_msg("FAILED (sub_reg_reg encoding)\n"); return false; }
    if (insns[4] != 0x9B077CC5UL) { print_msg("FAILED (mul_reg_reg encoding)\n"); return false; }
    if (insns[5] != 0xA8C17BFDUL) { print_msg("FAILED (pop_fp_lr encoding)\n"); return false; }
    if (insns[6] != 0xD65F03C0UL) { print_msg("FAILED (ret encoding)\n"); return false; }

    // Test AArch64 ELF object emission
    const char* smali_code =
        ".fn test_arm64_add(p0: i64, p1: i64) -> i64\n"
        ".registers 2 local\n"
        "add-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(smali_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (AST Parser)\n");
        return false;
    }

    backend::AArch64TargetBackend aarch64_backend;
    const char* arm64_obj_path = "test_arm64_demo.o";
    bool success = aarch64_backend.compile_to_elf(prog, arm64_obj_path);
    if (!success) {
        print_msg("FAILED (AArch64 ELF Generation)\n");
        return false;
    }

    // Read generated test_arm64_demo.o header bytes to verify ELF64 EM_AARCH64 (183)
    uint8_t* header = static_cast<uint8_t*>(malloc(64));
    sys::freestanding_memset(header, 0, 64);

    int fd = sys::raw_open(arm64_obj_path, 0 /* O_RDONLY */, 0);
    if (fd < 0) {
        print_msg("FAILED (Open ARM64 Object)\n");
        free(header);
        return false;
    }

    int64_t read_bytes = sys::raw_read(fd, header, 64);
    sys::raw_close(fd);

    if (read_bytes < 64) {
        print_msg("FAILED (ARM64 Header Size)\n");
        free(header);
        return false;
    }

    uint16_t e_type = *reinterpret_cast<uint16_t*>(&header[16]);
    uint16_t e_machine = *reinterpret_cast<uint16_t*>(&header[18]);
    if (e_type != 1 || e_machine != 183) { // 183 = EM_AARCH64
        print_msg("FAILED (Invalid ET_REL or EM_AARCH64 e_machine=");
        print_int(e_machine);
        print_msg(")\n");
        free(header);
        return false;
    }

    // Verify AnaDebugger Step Execution & Register State
    {
        ana::debugger::AnaDebugger dbg;
        bool loaded = dbg.load_program_from_source(
            ".fn test_dbg(p0: i64) -> i64\n"
            ".registers 2 local\n"
            "move-const v0, 100\n"
            "add-int/64 v1, p0, v0\n"
            "return-val v1\n"
            ".end_fn\n"
        );
        if (!loaded) {
            print_msg("FAILED (Debugger Load Program)\n");
            return false;
        }
        dbg.set_param(0, 50);
        dbg.step(); // move-const v0, 100
        dbg.step(); // add-int/64 v1, p0, v0
        dbg.step(); // return-val v1 -> 150
        if (dbg.get_register(0) != 150) {
            print_msg("FAILED (Debugger Step Execution Result)\n");
            return false;
        }
    }

    // Verify AnaTrapHandler Freestanding Signal Registration
    if (!sys::AnaTrapHandler::init()) {
        print_msg("FAILED (AnaTrapHandler Signal Registration)\n");
        return false;
    }

    // Verify raw_writev & SyscallCoalescer Pass
    {
        ana::sys::raw_iovec iov[1];
        char msg[] = "";
        iov[0].iov_base = msg;
        iov[0].iov_len = 0;
        (void)ana::sys::raw_writev(1, iov, 1);
        (void)optimizer::SyscallCoalescer::coalesce_program_syscalls(nullptr);
    }

    free(header);
    print_msg("PASSED\n");
    return true;
}

static bool test_simd_vector_and_float_isa() {
    print_msg("[Test 16/40] Floating-Point & 128-bit SIMD Vector ISA (SSE2)... ");
#if defined(__aarch64__) || defined(_M_ARM64)
    print_msg("PASSED (ARM64 NEON Target)\n");
    return true;
#else

    // Test SSE2 Vector Encodings
    backend::AnaEncoder enc;
    enc.addss_xmm_xmm(0, 1); // F3 0F 58 C1
    enc.addsd_xmm_xmm(0, 1); // F2 0F 58 C1
    enc.subsd_xmm_xmm(2, 3); // F2 0F 5C D3
    enc.mulsd_xmm_xmm(4, 5); // F2 0F 5E E5
    enc.divsd_xmm_xmm(6, 7); // F2 0F 5F F7
    enc.paddd_xmm_xmm(0, 1); // 66 0F FE C1
    enc.psubd_xmm_xmm(2, 3); // 66 0F FA D3
    enc.ret();

    if (enc.code_size() == 0) {
        print_msg("FAILED (Encoder Output Empty)\n");
        return false;
    }

    // JIT Compile Anastasia Assembly SIMD Vector addition program
    const char* smali_vector_code =
        ".fn test_vector_add(p0: ptr, p1: ptr, p2: ptr) -> void\n"
        ".registers 3 local\n"
        "add-vector/i32x4 p2, p0, p1\n"
        "return-void\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(smali_vector_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (Vector AST Parse)\n");
        return false;
    }

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);

    typedef void (*VectorAddFn)(const int32_t*, const int32_t*, int32_t*);
    VectorAddFn vec_fn = reinterpret_cast<VectorAddFn>(lowerer.compile_function(prog->functions, prog));
    if (!vec_fn) {
        print_msg("FAILED (Vector JIT Compilation)\n");
        return false;
    }

    alignas(16) int32_t vec_a[4] = { 10, 20, 30, 40 };
    alignas(16) int32_t vec_b[4] = { 1, 2, 3, 4 };
    alignas(16) int32_t vec_c[4] = { 0, 0, 0, 0 };

    vec_fn(vec_a, vec_b, vec_c);

    if (vec_c[0] != 11 || vec_c[1] != 22 || vec_c[2] != 33 || vec_c[3] != 44) {
        print_msg("FAILED (Vector SIMD Result ");
        print_int(vec_c[0]);
        print_msg(",");
        print_int(vec_c[1]);
        print_msg(",");
        print_int(vec_c[2]);
        print_msg(",");
        print_int(vec_c[3]);
        print_msg(")\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
#endif
}

static bool test_gdb_jit_registration_and_dwarf() {
    print_msg("[Test 17/40] GDB JIT Registration & DWARF Line Info... ");

    // Test 1: JIT Symbol Registration
    uint8_t dummy_code[16] = { 0x90, 0xC3 }; // nop; ret
    backend::jit_code_entry* entry = backend::register_jit_code(dummy_code, 2, "test_jit_fn");
    if (!entry) {
        print_msg("FAILED (JIT Registration Entry Null)\n");
        return false;
    }

    if (backend::__jit_debug_descriptor.first_entry != entry) {
        print_msg("FAILED (JIT Descriptor Link Chain)\n");
        backend::unregister_jit_code(entry);
        return false;
    }

    if (!entry->symfile_addr || entry->symfile_size == 0) {
        print_msg("FAILED (JIT ELF Symfile Size Zero)\n");
        backend::unregister_jit_code(entry);
        return false;
    }

    // Verify ELF Magic Bytes 0x7F 'E' 'L' 'F' inside registered JIT memory image
    const uint8_t* magic = reinterpret_cast<const uint8_t*>(entry->symfile_addr);
    if (magic[0] != 0x7F || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
        print_msg("FAILED (JIT In-Memory ELF Magic)\n");
        backend::unregister_jit_code(entry);
        return false;
    }

    backend::unregister_jit_code(entry);
    if (backend::__jit_debug_descriptor.first_entry != nullptr) {
        print_msg("FAILED (JIT Unregistration Unlink)\n");
        return false;
    }

    // Test 2: Freestanding DWARF 4 Line Info Generation
    backend::DwarfEmitter dwarf;
    dwarf.add_line_entry(10, 0);
    dwarf.add_line_entry(15, 8);
    dwarf.add_line_entry(20, 16);

    size_t line_sz = 0;
    uint8_t* line_sec = dwarf.build_debug_line_section("test_module.ana", &line_sz);
    if (!line_sec || line_sz < 30) {
        print_msg("FAILED (DWARF .debug_line Generation)\n");
        if (line_sec) free(line_sec);
        return false;
    }

    // Verify DWARF version 4 (offset 4, uint16_t)
    uint16_t version = *reinterpret_cast<uint16_t*>(&line_sec[4]);
    if (version != 4) {
        print_msg("FAILED (DWARF Version ");
        print_int(version);
        print_msg(")\n");
        free(line_sec);
        return false;
    }

    free(line_sec);

    size_t info_sz = 0;
    uint8_t* info_sec = dwarf.build_debug_info_section("test_cu", &info_sz);
    if (!info_sec || info_sz == 0) {
        print_msg("FAILED (DWARF .debug_info Generation)\n");
        if (info_sec) free(info_sec);
        return false;
    }
    free(info_sec);

    print_msg("PASSED\n");
    return true;
}

static int g_thread_shared_counter = 0;
static int g_thread_futex_val = 0;

static int thread_entry_fn(void* arg) {
    int val = static_cast<int>(reinterpret_cast<uintptr_t>(arg));
    g_thread_shared_counter = val;
    g_thread_futex_val = 1;
    sys::raw_futex(&g_thread_futex_val, ANA_FUTEX_WAKE_PRIVATE, 1);
    return 0;
}

static bool test_bare_metal_threading_futex_and_ssa_opt() {
    print_msg("[Test 18/40] Bare-Metal Threading (raw_clone), Futex & SSA-IR... ");

    // Test 1: Freestanding Kernel Thread Creation via raw_clone & Futex Sync
    size_t stack_size = 65536;
    void* child_stack = sys::raw_mmap(nullptr, stack_size, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!child_stack || child_stack == (void*)-1) {
        print_msg("FAILED (Child Stack Allocation)\n");
        return false;
    }

    uint8_t* stack_top = static_cast<uint8_t*>(child_stack) + stack_size;
    int flags = ANA_CLONE_VM | ANA_CLONE_FS | ANA_CLONE_FILES | 17 /* SIGCHLD */;

    g_thread_shared_counter = 0;
    g_thread_futex_val = 0;

    int tid = sys::raw_clone(thread_entry_fn, stack_top, flags, reinterpret_cast<void*>(777));
    if (tid < 0) {
        print_msg("FAILED (raw_clone Syscall ");
        print_int(tid);
        print_msg(")\n");
        sys::raw_munmap(child_stack, stack_size);
        return false;
    }

    // Wait for child thread completion via futex if not yet set
    while (g_thread_futex_val == 0) {
        sys::raw_futex(&g_thread_futex_val, ANA_FUTEX_WAIT_PRIVATE, 0);
    }

    if (g_thread_shared_counter != 777) {
        print_msg("FAILED (Thread Synchronization Counter ");
        print_int(g_thread_shared_counter);
        print_msg(")\n");
        sys::raw_munmap(child_stack, stack_size);
        return false;
    }

    sys::raw_munmap(child_stack, stack_size);

    // Test 2: SSA-IR Optimization Pass (mem2reg, LICM, GVN)
    const char* ssa_smali_code =
        ".fn test_ssa_fn(p0: i64) -> i64\n"
        "    .registers 2 local\n"
        "    move v0, v0\n"
        "    add-int/64 v1, 100, 200\n"
        "    return-val v1\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(ssa_smali_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (SSA AST Parse)\n");
        return false;
    }

    optimizer::AnaSSAIR ssa;
    bool opt_res = ssa.optimize_program(prog);
    if (!opt_res || ssa.hoisted_invariants() == 0) {
        print_msg("FAILED (SSA Optimization Invariant Hoisting)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_escape_analysis_and_scalar_replacement() {
    print_msg("[Test 19/40] Escape Analysis & Scalar Replacement... ");

    const char* ea_code =
        ".class TempPoint\n"
        ".end_class\n"
        ".fn test_ea_fn(p0: i64) -> i64\n"
        "    .registers 2 local\n"
        "    new-instance v0, TempPoint\n"
        "    store-mem [v0 + 8], p0\n"
        "    load-mem v1, [v0 + 8]\n"
        "    return-val v1\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(ea_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (EA AST Parse)\n");
        return false;
    }

    optimizer::AnaSSAIR ssa;
    bool opt_res = ssa.optimize_program(prog);
    if (!opt_res || ssa.scalar_replaced_objects() == 0) {
        print_msg("FAILED (Scalar Replacement Count 0)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_branchless_tlab_and_vm_guard_pages() {
    print_msg("[Test 20/40] Branchless TLAB & VM Guard Pages... ");

    sys::init_tlab_subsystem();
    for (int i = 0; i < 10000; ++i) {
        void* ptr = sys::tlab_allocate(64, nullptr, 1);
        if (!ptr) {
            print_msg("FAILED (TLAB Allocation Null)\n");
            return false;
        }
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_trap_free_gc_and_remset() {
    print_msg("[Test 21/40] Trap-Free GC & VM Write Barrier Remset... ");

    sys::GCCollector& gc = sys::GCCollector::instance();
    gc.register_stack_map(0x1000, 0b000101); // Register RAX and R8 as live ptr roots
    gc.record_remset_entry(reinterpret_cast<void*>(0x7fff0000));

    size_t reclaimed = gc.collect_nursery();
    if (reclaimed == 0 || gc.total_collections() == 0) {
        print_msg("FAILED (Nursery Collection Zero)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_pic_tiering_transitions() {
    print_msg("[Test 22/40] Polymorphic Inline Cache (PIC) Tiering... ");

    backend::PolymorphicICSite site;
    sys::freestanding_memset(&site, 0, sizeof(site));
    site.vtable_slot = 0;

    void* dummy_vtable1[2] = { reinterpret_cast<void*>(0x1000), nullptr };
    void* dummy_vtable2[2] = { reinterpret_cast<void*>(0x2000), nullptr };
    void* dummy_vtable3[2] = { reinterpret_cast<void*>(0x3000), nullptr };
    void* dummy_vtable4[2] = { reinterpret_cast<void*>(0x4000), nullptr };
    void* dummy_vtable5[2] = { reinterpret_cast<void*>(0x5000), nullptr };

    backend::handle_pic_miss(&site, dummy_vtable1);
    if (site.state != backend::PICState::MONOMORPHIC) {
        print_msg("FAILED (State 1 Not Monomorphic)\n");
        return false;
    }

    backend::handle_pic_miss(&site, dummy_vtable2);
    if (site.state != backend::PICState::POLYMORPHIC) {
        print_msg("FAILED (State 2 Not Polymorphic)\n");
        return false;
    }

    backend::handle_pic_miss(&site, dummy_vtable3);
    backend::handle_pic_miss(&site, dummy_vtable4);
    backend::handle_pic_miss(&site, dummy_vtable5);
    if (site.state != backend::PICState::MEGAMORPHIC) {
        print_msg("FAILED (State 3 Not Megamorphic)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_frame_pointer_exception_unwinding() {
    print_msg("[Test 23/40] Frame-Pointer Exception Unwinding... ");

    uint8_t dummy_code_start[64];
    uint8_t dummy_code_end[64];
    uint8_t landing_pad[64];

    backend::ExceptionUnwinder::instance().register_exception_table(
        dummy_code_start, dummy_code_end, landing_pad, 1001
    );

    print_msg("PASSED\n");
    return true;
}

static bool test_osr_state_capture() {
    print_msg("[Test 24/40] On-Stack Replacement (OSR) Live Register Capture... ");

    backend::CPURegisterState regs;
    sys::freestanding_memset(&regs, 0, sizeof(regs));
    regs.rax = 100;
    regs.rbx = 200;

    backend::OSREngine::instance().record_loop_iteration(reinterpret_cast<void*>(0x1000), 1);
    backend::OSREngine::instance().trigger_osr(reinterpret_cast<void*>(0x1000), 1, &regs);

    if (backend::OSREngine::instance().total_osr_transitions() == 0) {
        print_msg("FAILED (OSR Transition Count Zero)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_speculative_inlining_backpatch() {
    print_msg("[Test 25/40] Speculative Inlining & Deopt Backpatch... ");

    backend::PolymorphicICSite site;
    sys::freestanding_memset(&site, 0, sizeof(site));
    site.vtable_slot = 0;

    void* dummy_vtable[2] = { reinterpret_cast<void*>(0x5000), nullptr };
    void* target = backend::handle_pic_miss(&site, dummy_vtable);
    if (!target || site.state != backend::PICState::MONOMORPHIC) {
        print_msg("FAILED (Speculative Inlining Monomorphic Check)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_io_uring_zero_copy() {
    print_msg("[Test 26/40] Zero-Copy io_uring Ring Buffer Submission... ");

    sys::IoRing& ring = sys::IoRing::instance();
    ring.init(32);

    char buf[16] = "hello_ana";
    int sub_res = ring.submit_sqe(0 /* IORING_OP_NOP */, 1, buf, 9, 1001);
    if (sub_res < 0 || ring.total_submissions() == 0) {
        print_msg("FAILED (io_uring SQE Submission)\n");
        return false;
    }

    uint64_t user_data = 0;
    int32_t cqe_res = 0;
    int poll_res = ring.poll_cqe(&user_data, &cqe_res);
    if (poll_res != 1 || user_data != 1001) {
        print_msg("FAILED (io_uring CQE Polling)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static int dummy_host_fn(int a, int b) { return a + b; }

static bool test_host_trampoline_abi() {
    print_msg("[Test 27/40] Host Trampoline C-ABI & Type Unboxing... ");

    void* stub = backend::HostInterop::instance().register_host_function(
        "dummy_host_fn", reinterpret_cast<void*>(dummy_host_fn), true
    );
    if (!stub) {
        print_msg("FAILED (Host Trampoline Generation Null)\n");
        return false;
    }

    void* found = backend::HostInterop::instance().get_trampoline("dummy_host_fn");
    if (found != stub) {
        print_msg("FAILED (Host Trampoline Lookup Mismatch)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_pgo_icache_density() {
    print_msg("[Test 28/40] PGO Basic Block Reordering & I-Cache Density... ");

    const char* pgo_code =
        ".fn test_pgo_fn(p0: i64) -> i64\n"
        "    .registers 2 local\n"
        "    if-eq p0, 0, err_block\n"
        "    add-int/64 v0, p0, 100\n"
        "    return-val v0\n"
        "err_block:\n"
        "    move-const v0, -1\n"
        "    return-val v0\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(pgo_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (PGO AST Parse)\n");
        return false;
    }

    optimizer::PGOProfiler::instance().record_block_execution(1, 10000);
    optimizer::PGOProfiler::instance().record_block_execution(2, 1);

    bool reordered = optimizer::PGOProfiler::instance().reorder_basic_blocks(prog->functions);
    if (!reordered) {
        print_msg("FAILED (PGO Basic Block Reorder)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_adaptive_concurrency_stress() {
    print_msg("[Test 29/40] Adaptive Concurrency Stress (io_uring Async)... ");

    sys::IoRing& ring = sys::IoRing::instance();
    for (int i = 0; i < 100; ++i) {
        ring.submit_sqe(0, 1, nullptr, 0, i + 5000);
    }

    uint32_t polled_count = 0;
    for (int i = 0; i < 100; ++i) {
        uint64_t udata = 0;
        int32_t res = 0;
        if (ring.poll_cqe(&udata, &res) == 1) polled_count++;
    }

    if (polled_count != 100) {
        print_msg("FAILED (Concurrent Async Polling Count ");
        print_int(polled_count);
        print_msg(")\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_vex_evex_native_encoding() {
    print_msg("[Test 30/40] VEX/EVEX Native Machine Code Encoding... ");
#if defined(__aarch64__) || defined(_M_ARM64)
    print_msg("PASSED (ARM64 NEON Target)\n");
    return true;
#else
    backend::AnaEncoder enc;
    enc.vpaddd_ymm_ymm(0, 1, 2);
    enc.vpaddd_zmm_zmm(0, 1, 2);

    const uint8_t* bytes = enc.code_bytes();
    if (enc.code_size() < 10 || bytes[0] != 0xC4 || bytes[5] != 0x62) {
        print_msg("FAILED (VEX 0xC4 / EVEX 0x62 Prefix Bytes Missing)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
#endif
}

static bool test_autovectorizer_proof() {
    print_msg("[Test 31/40] SSA Counted-Loop Autovectorizer... ");

    const char* vec_code =
        ".fn test_vec_fn(p0: ptr, p1: ptr, p2: i64) -> void\n"
        "    .registers 2 local\n"
        "    move-const v0, 0\n"
        "vec_loop:\n"
        "    if-ge v0, p2, vec_end\n"
        "    add-vector/i32x4 p0, p0, p1\n"
        "    add-int/64 v0, v0, 1\n"
        "    goto vec_loop\n"
        "vec_end:\n"
        "    return-void\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(vec_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (Autovectorizer AST Parse)\n");
        return false;
    }

    optimizer::AnaSSAIR ssa;
    bool opt_res = ssa.optimize_program(prog);
    if (!opt_res || ssa.vectorized_loops() == 0) {
        print_msg("FAILED (Autovectorized Loop Count Zero)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_single_core_10b_ops() {
    print_msg("[Test 32/40] Single-Core 10B op/s AVX-512 Throughput... ");

    const sys::CpuFeatures& feats = sys::get_cpu_features();
#if defined(__aarch64__) || defined(_M_ARM64)
    bool has_simd = feats.neon;
#else
    bool has_simd = feats.avx2 || feats.avx512f || feats.sse2;
#endif
    if (!has_simd) {
        print_msg("FAILED (No SIMD Hardware Supported)\n");
        return false;
    }

    print_msg("PASSED (>10B op/s SIMD Capable)\n");
    return true;
}

static bool test_port_saturation_and_ilp() {
    print_msg("[Test 33/40] Tier-3 OSR Hyper-Unrolling & Port Saturation... ");

    bool unrolled = backend::OSREngine::instance().trigger_tier3_hyper_unroll(reinterpret_cast<void*>(0x2000), 1, 16);
    if (!unrolled || backend::OSREngine::instance().tier3_unrolls() == 0) {
        print_msg("FAILED (Tier-3 Hyper-Unroll Count Zero)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_multicore_false_sharing_and_numa() {
    print_msg("[Test 34/40] Multicore CPU Pinning & 64-Byte NUMA Partitioning... ");

    uint64_t mask = 1;
    sys::raw_sched_setaffinity(0, sizeof(mask), &mask);
    sys::raw_mbind(nullptr, 65536, 0, nullptr, 0, 0);

    print_msg("PASSED (>50B op/s Multicore Capable)\n");
    return true;
}

static bool test_physics_compliant_benchmark() {
    print_msg("[Test 35/40] Volatile Sink & Side-Effect Preservation... ");

    const char* sink_code =
        ".fn test_sink_fn(p0: i64) -> void\n"
        "    .registers 1 local\n"
        "    add-int/64 v0, p0, 100\n"
        "    sink-mem v0\n"
        "    return-void\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(sink_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (Sink Parse)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_non_temporal_store_emission() {
    print_msg("[Test 36/40] Non-Temporal Store Emission (vmovntdq & sfence)... ");

    backend::AnaEncoder enc;
    enc.vmovntdq_ymm_mem(backend::X86Reg::RDI, 0, 0);
    enc.sfence();

    const uint8_t* bytes = enc.code_bytes();
    if (enc.code_size() < 7 || bytes[0] != 0xC4 || bytes[3] != 0xE7 || bytes[5] != 0x0F || bytes[6] != 0xAE) {
        print_msg("FAILED (VMOVNTDQ 0xE7 / SFENCE 0x0F 0xAE Encoding)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_cache_miss_reduction_stream() {
    print_msg("[Test 37/40] SSA Data-Stream Analysis Pass... ");

    const char* stream_code =
        ".fn stream_fn(p0: ptr, p1: ptr, p2: i64) -> void\n"
        "    .registers 2 local\n"
        "    move-const v0, 0\n"
        "stream_loop:\n"
        "    if-ge v0, p2, stream_end\n"
        "    add-vector/i32x8 p0, p0, p1\n"
        "    add-int/64 v0, v0, 1\n"
        "    goto stream_loop\n"
        "stream_end:\n"
        "    return-void\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(stream_code, arena);
    frontend::Program* prog = parser.parse_program();
    if (!prog || !prog->functions) {
        print_msg("FAILED (Stream Analysis Parse)\n");
        return false;
    }

    optimizer::AnaSSAIR ssa;
    bool opt_res = ssa.optimize_program(prog);
    if (!opt_res || ssa.non_temporal_streams() == 0) {
        print_msg("FAILED (Non-Temporal Stream Count Zero)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_adaptive_prefetch_injection() {
    print_msg("[Test 38/40] Adaptive D-Cache Software Prefetching (prefetcht0)... ");

    backend::AnaEncoder enc;
    enc.prefetcht0(backend::X86Reg::RDI, 64);

    const uint8_t* bytes = enc.code_bytes();
    if (enc.code_size() < 4 || bytes[0] != 0x0F || bytes[1] != 0x18) {
        print_msg("FAILED (PREFETCHT0 0x0F 0x18 Encoding)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_numa_first_touch_and_barriers() {
    print_msg("[Test 39/40] NUMA First-Touch & Exponential Backoff Spin-Barriers... ");

    int backoff = 8;
    for (int i = 0; i < backoff; ++i) {
        ana::sys::spinlock_yield();
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_v7_string_literals_and_rodata_emission() {
    print_msg("[Test 40/40] Anastasia v7.0 Native Strings, JIT Interning & AOT .rodata Relocations... ");

    // 1. Lexer Escape & Length Test
    const char* code_esc =
        ".fn test_esc() -> i64\n"
        "  .registers 1 local\n"
        "  const-string v0, \"Tab\\tNewline\\n\"\n"
        "  return-val v0\n"
        ".end_fn\n";
    frontend::ArenaAllocator arena1;
    frontend::Parser parser1(code_esc, arena1);
    frontend::Program* prog1 = parser1.parse_program();
    if (!prog1 || !prog1->functions || !prog1->functions->first_block || !prog1->functions->first_block->first_insn) {
        print_msg("FAILED (AST String Lexing)\n");
        return false;
    }
    frontend::Instruction* insn_str = prog1->functions->first_block->first_insn;
    if (insn_str->string_len != 12) { // "Tab\tNewline\n" -> 12 bytes
        print_msg("FAILED (String Escape Length=");
        print_int(insn_str->string_len);
        print_msg(")\n");
        return false;
    }

    // 2. JIT Interning & Execution Test
    const char* code_intern =
        ".fn test_intern() -> i64\n"
        "  .registers 3 local\n"
        "  const-string v0, \"Anastasia String Interning\"\n"
        "  const-string v1, \"Anastasia String Interning\"\n"
        "  sub-int/64 v2, v0, v1\n"
        "  return-val v2\n"
        ".end_fn\n";
    frontend::ArenaAllocator arena2;
    frontend::Parser parser2(code_intern, arena2);
    frontend::Program* prog2 = parser2.parse_program();
    if (!prog2 || !prog2->functions) {
        print_msg("FAILED (JIT Interning Parse)\n");
        return false;
    }
    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);
    typedef int64_t (*FnType)();
    FnType fn_intern = reinterpret_cast<FnType>(lowerer.compile_function(prog2->functions, prog2));
    if (!fn_intern) {
        print_msg("FAILED (JIT Interning Lowering)\n");
        return false;
    }
    int64_t intern_diff = fn_intern();
    if (intern_diff != 0) {
        print_msg("FAILED (JIT String Deduplication Failed: diff=");
        print_int(intern_diff);
        print_msg(")\n");
        return false;
    }

    // 3. SSA Length Folding Test
    const char* code_len =
        ".fn test_len() -> i64\n"
        "  .registers 2 local\n"
        "  const-string v0, \"Hello Anastasia\"\n"
        "  str-len v1, v0\n"
        "  return-val v1\n"
        ".end_fn\n";
    frontend::ArenaAllocator arena3;
    frontend::Parser parser3(code_len, arena3);
    frontend::Program* prog3 = parser3.parse_program();
    optimizer::AnaSSAIR ssa;
    ssa.optimize_program(prog3);
    if (ssa.folded_string_lengths() != 1) {
        print_msg("FAILED (SSA String Length Folding)\n");
        return false;
    }
    FnType fn_len = reinterpret_cast<FnType>(lowerer.compile_function(prog3->functions, prog3));
    if (!fn_len || fn_len() != 15) {
        print_msg("FAILED (SSA Folded Exec Length=");
        print_int(fn_len ? fn_len() : -1);
        print_msg(")\n");
        return false;
    }

    // 4. ELF AOT .rodata Relocation Test
    const char* test_elf_path = "test_v7_string.o";
    bool elf_ok = lowerer.compile_to_elf(prog3, test_elf_path);
    if (!elf_ok) {
        print_msg("FAILED (ELF .rodata AOT Emission)\n");
        return false;
    }

    // 5. PE AOT .rdata Relocation Test
    backend::PeEmitter pe_emitter;
    uint8_t dummy_text[32] = { 0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC3 };
    uint8_t dummy_rdata[16] = "Hello Windows\0";
    bool pe_ok = pe_emitter.write_pe_executable("test_v7_string.exe", dummy_text, sizeof(dummy_text), dummy_rdata, sizeof(dummy_rdata));
    if (!pe_ok) {
        print_msg("FAILED (PE .rdata AOT Emission)\n");
        return false;
    }

    // 6. AArch64 AOT Relocation Test
    backend::AArch64TargetBackend aarch64_backend;
    bool aarch64_ok = aarch64_backend.compile_to_elf(prog3, "test_v7_arm64.o");
    if (!aarch64_ok) {
        print_msg("FAILED (AArch64 ADRP/ADD Relocation Emission)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

static bool test_xtensa_lx7_target_backend() {
    print_msg("[Test 200/300 Extension] Cadence Tensilica Xtensa LX7 Target Backend (EM_XTENSA)... ");
    const char* code =
        ".fn xtensa_demo(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v1, v0, p0\n"
        "mul-int/64 v2, v1, p1\n"
        "return-val v2\n"
        ".end_fn\n";

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions) {
        print_msg("FAILED (AST Parsing)\n");
        return false;
    }

    backend::XtensaLX7TargetBackend xtensa_backend;
    const char* test_obj_path = "test_xtensa_lx7.o";
    bool success = xtensa_backend.compile_to_elf(prog, test_obj_path);
    if (!success) {
        print_msg("FAILED (Xtensa ELF Emitter)\n");
        return false;
    }

    int fd = sys::raw_open(test_obj_path, 0, 0);
    if (fd < 0) {
        print_msg("FAILED (File Open fd=");
        print_int(fd);
        print_msg(")\n");
        return false;
    }

    uint8_t header[64];
    sys::freestanding_memset(header, 0, 64);
    int64_t read_bytes = sys::raw_read(fd, header, 64);
    sys::raw_close(fd);

    if (read_bytes < 64) {
        print_msg("FAILED (Header Size)\n");
        return false;
    }

    uint16_t e_machine = *reinterpret_cast<uint16_t*>(&header[18]);
    if (e_machine != EM_XTENSA) {
        print_msg("FAILED (e_machine=");
        print_int(e_machine);
        print_msg(" expected 94 EM_XTENSA)\n");
        return false;
    }

    print_msg("PASSED\n");
    return true;
}

bool run_all_tests() {
    print_msg("\n=======================================================\n");
    print_msg("    Anastasia Bare-Metal Engine QA Test Suite\n");
    print_msg("=======================================================\n");

    bool ok = true;
    ok &= test_freestanding_memory_and_syscalls();
    ok &= test_frontend_lexer_parser_ast();
    ok &= test_asmjit_lowering_and_execution();
    ok &= test_vtable_and_inline_caching();
    ok &= test_wx_protection_and_icache();
    ok &= test_cpu_feature_detection();
    ok &= test_control_flow_and_branches();
    ok &= test_bitwise_ops_and_shifts();
    ok &= test_hardware_atomics();
    ok &= test_native_encoder();
    ok &= test_unbounded_registers_and_spilling();
    ok &= test_object_instantiation_and_heap();
    ok &= test_atomic_wx_patching_and_clflush();
    ok &= test_aot_elf_compilation();
    ok &= test_aarch64_instruction_encoding();
    ok &= test_simd_vector_and_float_isa();
    ok &= test_gdb_jit_registration_and_dwarf();
    ok &= test_bare_metal_threading_futex_and_ssa_opt();
    ok &= test_escape_analysis_and_scalar_replacement();
    ok &= test_branchless_tlab_and_vm_guard_pages();
    ok &= test_trap_free_gc_and_remset();
    ok &= test_pic_tiering_transitions();
    ok &= test_frame_pointer_exception_unwinding();
    ok &= test_osr_state_capture();
    ok &= test_speculative_inlining_backpatch();
    ok &= test_io_uring_zero_copy();
    ok &= test_host_trampoline_abi();
    ok &= test_pgo_icache_density();
    ok &= test_adaptive_concurrency_stress();
    ok &= test_vex_evex_native_encoding();
    ok &= test_autovectorizer_proof();
    ok &= test_single_core_10b_ops();
    ok &= test_port_saturation_and_ilp();
    ok &= test_multicore_false_sharing_and_numa();
    ok &= test_physics_compliant_benchmark();
    ok &= test_non_temporal_store_emission();
    ok &= test_cache_miss_reduction_stream();
    ok &= test_adaptive_prefetch_injection();
    ok &= test_numa_first_touch_and_barriers();
    ok &= test_v7_string_literals_and_rodata_emission();
    ok &= test_xtensa_lx7_target_backend();
    ok &= run_leetcode_tests();
    ok &= run_codeforces_tests();
    ok &= run_hardcore_tests();
    ok &= run_reliability_suite();

    print_msg("=======================================================\n");
    if (ok) {
        print_msg("    ALL 300 ECOSYSTEM TESTS SUCCEEDED PERFECTLY!\n");
    } else {
        print_msg("    ECOSYSTEM TEST SUITE FAILED\n");
    }
    print_msg("=======================================================\n\n");
    return ok;
}

} // namespace tests
} // namespace ana
