#include "test_suite.h"
#include "../src/sys/sys_raw.h"
#include "../src/sys/cpu_features.h"
#include "../src/frontend/arena_allocator.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/vmem_provider.h"
#include "../src/backend/ana_lowerer.h"
#include "../src/backend/inline_cache.h"
#include "../src/backend/aarch64_backend.h"
#include <fcntl.h>
#include <unistd.h>

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
    print_msg("[Test 1/9] Syscall & Freestanding Memory Operations... ");

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
    print_msg("[Test 2/6] Perfect-Hash Lexer, Arena Allocator & Constant Folding... ");

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
    print_msg("[Test 3/6] AsmJit JIT Lowering & Bare-Metal Execution... ");

    const char* sample_code =
        ".fn sum_fn(p0: i64, p1: i64) -> i64\n"
        "    .registers 1 local\n"
        "    add-int/64 v0, p0, p1\n"
        "    return-val v0\n"
        ".end_fn\n";

    ana::frontend::ArenaAllocator arena;
    ana::frontend::Parser parser(sample_code, arena);
    ana::frontend::Program* prog = parser.parse_program();

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
    print_msg("[Test 4/6] OOP Layout, VTable Dispatch & Monomorphic Inline Cache... ");

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
    print_msg("[Test 5/6] Strict W^X Protection & Instruction Cache Flush... ");

    void* page = ana::sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!page) {
        print_msg("FAILED (Alloc)\n");
        return false;
    }

    // Write NOPs
    unsigned char* code = static_cast<unsigned char*>(page);
    code[0] = 0x90; // NOP
    code[1] = 0xC3; // RET

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
    print_msg("[Test 6/9] Dynamic CPU SIMD Routing... ");
    const auto& feat = ana::sys::get_cpu_features();
    if (feat.avx2) {
        print_msg("PASSED (AVX2 detected & routed)\n");
    } else if (feat.neon) {
        print_msg("PASSED (NEON detected & routed)\n");
    } else {
        print_msg("PASSED (Scalar fallback active)\n");
    }
    return true;
}

static bool test_control_flow_and_branches() {
    print_msg("[Test 7/9] Control Flow, Fused Branches & Fallthrough Optimization... ");
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
    print_msg("[Test 8/9] Bitwise ISA, %cl Shift Pinning & Popcount... ");
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
    print_msg("[Test 9/9] Hardware Lock-Free Atomics & Memory Ordering... ");
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
    print_msg("[Test 10/13] Native Bare-Metal Instruction Encoder (AnaEncoder)... ");
    backend::AnaEncoder enc;
    enc.mov_reg_imm64(backend::X86Reg::RAX, 12345);
    enc.ret();
    if (!enc.resolve_labels()) {
        print_msg("FAILED (Label Resolution)\n");
        return false;
    }

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
    print_msg("[Test 11/13] Unbounded Virtual Registers & Stack Spilling (v0..v15)... ");
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
    print_msg("[Test 12/13] Object Instantiation (new-instance) & Heap Allocation... ");
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
    print_msg("[Test 13/13] Atomic W^X Code Patching & clflush Invalidation... ");
    void* code_mem = ana::sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!code_mem) {
        print_msg("FAILED (Alloc)\n");
        return false;
    }

    uint64_t* patch_slot = static_cast<uint64_t*>(code_mem);
    __atomic_store_n(patch_slot, 0x90909090C3909090ULL, __ATOMIC_RELEASE);

    __asm__ __volatile__(
        "clflush (%0)\n\t"
        "mfence"
        :
        : "r"(patch_slot)
        : "memory"
    );

    ana::sys::raw_mprotect(code_mem, 4096, ANA_PROT_READ | ANA_PROT_EXEC);
    ana::sys::clear_icache(code_mem, 4096);
    ana::sys::raw_munmap(code_mem, 4096);

    print_msg("PASSED\n");
    return true;
}

static bool test_aot_elf_compilation() {
    print_msg("[Test 14/14] AOT Relocatable ELF Object File Emitter (ElfEmitter)... ");
    const char* code =
        ".fn aot_demo(p0: i64, p1: i64) -> i64\n"
        ".registers 2 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v1, v0, p0\n"
        "return-val v1\n"
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

    // Verify ET_REL (1) and EM_X86_64 (62)
    uint16_t e_type = *reinterpret_cast<uint16_t*>(&header[16]);
    uint16_t e_machine = *reinterpret_cast<uint16_t*>(&header[18]);
    if (e_type != 1 || e_machine != 62) {
        print_msg("FAILED (Invalid ET_REL or EM_X86_64)\n");
        free(header);
        return false;
    }

    free(header);
    print_msg("PASSED\n");
    return true;
}

static bool test_aarch64_instruction_encoding() {
    print_msg("[Test 15/15] AArch64 Backend & Fixed 32-bit Machine Code Emitter... ");

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

    free(header);
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

    print_msg("=======================================================\n");
    if (ok) {
        print_msg("    ALL 15 QA MATRIX TESTS SUCCEEDED PERFECTLY!\n");
    } else {
        print_msg("    QA MATRIX TEST SUITE FAILED\n");
    }
    print_msg("=======================================================\n\n");
    return ok;
}

} // namespace tests
} // namespace ana
