# Anastasia Engine v7.1: Technical Specification & Ecosystem Reference

## 1. Overview & Philosophy

**Anastasia v7.1** is an embeddable, adaptive, bare-metal, zero-CRT compiler ecosystem and execution engine for **Anastasia Assembly** (`.ana`). Designed for maximum execution throughput, zero runtime overhead, adaptive dynamic tiering, non-temporal data streaming, and zero-copy hardware I/O, Anastasia compiles high-level Anastasia Assembly programs directly to native x86_64 and AArch64 (ARM64) machine code at runtime (JIT) or emits relocatable ELF object files (`.o`) and standalone Windows PE32+ executables (`.exe`) for static linking (AOT) without linking against standard C runtimes (`libc`, `libstdc++`), C++ standard libraries, or third-party dependencies.

### Core Architectural Principles
* **Freestanding Bare-Metal Execution**: Operates exclusively under GCC/Clang freestanding flags (`-ffreestanding`, `-nostdlib`, `-nodefaultlibs`, `-fno-exceptions`, `-fno-rtti`) with direct assembly syscall boundaries (`raw_mmap`, `raw_mprotect`, `raw_munmap`, `raw_write`, `raw_read`, `raw_open`, `raw_close`, `raw_clone`, `raw_futex`, `raw_exit`, `raw_sched_setaffinity`, `raw_mbind`, `raw_io_uring_setup`, `raw_io_uring_enter`). Completely purged external dependencies like `asmjit` in favor of native freestanding machine code encoders.
* **Native String Literals & Zero-Linker `.rodata` Relocations**: Implements `const-string` literal parsing in the AST frontend. Operates zero-copy length tracking at parse time to eliminate `strlen()` runtime overhead. JIT mode dynamically interns strings into read-only executable memory pools, while AOT mode (`ElfEmitter`) acts as its own native linker by emitting `.rodata` sections and patching RIP-relative relocations (`R_X86_64_PC32` / `R_AARCH64_ADR_PREL_PG_HI21`) without needing `ld` or `link.exe`.
* **JIT / AOT Duality Engine**: Dual emission targets via `AnaTargetBackend`: `MemoryTarget` (JIT executable memory), `ElfEmitter` (relocatable 64-bit ELF object files with symbol tables and relocations), and `PeEmitter` (native 64-bit Windows PE32+ `.exe` executables).
* **Multi-Architecture Target Portability**: Pluggable backend architecture supporting **x86_64** (`X86_64TargetBackend` / `AnaEncoder`) and **AArch64 / ARM64** (`AArch64TargetBackend` / `AArch64Encoder`).
* **VEX (256-bit AVX2) & EVEX (512-bit AVX-512) Native Encoder**: Native byte-packing encoders (`emit_vex3`, `emit_evex`) across 256-bit `YMM` and 512-bit `ZMM` vector register files (`AnaEncoder`). Dynamic CPU routing (`cpuid` Leaf 7) selects the widest available vector width on boot.
* **SSA Counted-Loop Autovectorizer**: SSA pass (`run_autovectorizer`) identifies induction variables and memory independence, transforming scalar loops into 256-bit (8-wide) or 512-bit (16-wide) SIMD vector loops with scalar tail fallbacks.
* **SSA Data-Stream Analysis & Non-Temporal Stores**: Memory stream analysis (`run_stream_analysis`) detects sequential array writes (>128 elements) without load-after-store dependencies, emitting non-temporal stores (`vmovntdq` / `movntdq`) followed by `sfence()` at loop exit to write directly to RAM without cache pollution.
* **Adaptive D-Cache Software Prefetching**: Dynamically injects software prefetch instructions (`prefetcht0`) `N` iterations ahead of array load pointers based on microarchitectural cache line size.
* **Exponential Backoff Spin-Barriers & NUMA First-Touch**: Spin-barriers execute an adaptive exponential backoff sequence (`pause` 8 $\to$ 16 $\to$ 32 $\to$ `raw_futex`). Array memory initialization loops execute *inside* `raw_clone` worker threads, enforcing local NUMA node page binding via `raw_mbind`.
* **Volatile Sink & Side-Effect Preservation**: `.ana` opcode `sink-mem` and C-ABI `ana_benchmark_consume(val)` mark values as volatile roots, prohibiting Dead Code Elimination (DCE) from stripping computation loops.
* **JIT Escape Analysis & Scalar Replacement**: Single-pass SSA escape analysis pass (`run_escape_analysis`) that scalar-replaces short-lived `new-instance` allocations directly into virtual registers/stack slots, achieving **0 heap allocations** for non-escaping objects.
* **Branchless TLAB Allocation & VM Guard Pages**: Fast-path Thread-Local Allocation Buffer (`tlab_allocate`) performing branchless pointer bump allocations (`mov`, `lea`, `mov`). Boundaries guarded by `PROT_NONE` pages; overruns trigger a freestanding `SIGSEGV` fault handler to allocate new 64 KB TLAB slabs transparently.
* **Trap-Free Precise GC & Virtual Memory Write Barriers**: Old Generation heap pages are protected as `PROT_READ`. Pointer stores execute in 1 cycle without write-barrier sequences. Inter-generational stores trigger VM page faults, recording target addresses into the Remembered Set (Remset).
* **Speculative Inlining & On-Stack Replacement (OSR)**: Monomorphic call sites (>10k iterations) are speculatively inlined. Loop safe-point back-edge counters trigger On-Stack Replacement (`OSREngine`), capturing live CPU registers (`RAX`–`R15`, `%rsp`, `%rbp`), compiling Tier-2/Tier-3 SSA-optimized loops, and shifting execution into the new loop frame mid-flight.
* **Zero-Copy Hardware Async I/O (`io_uring`)**: `IoRing` allocates Submission Queue (SQ) and Completion Queue (CQ) ring buffers via `raw_mmap`. Opcode `io-submit` lowers directly to ring memory stores followed by `sys_io_uring_enter` (0 user-space buffer copies).

---

## 2. System Architecture

```
 ┌───────────────────────────────────────────────────────────────────────────┐
 │                         Anastasia Frontend Core                           │
 │  Ana Lexer ──> Ana Parser ──> AST ──> SSA Optimizer & Vectorizer           │
 │  Arena-Based AST Memory (Zero-CRT, Lock-Free Thread-Local Arena)          │
 └─────────────────────────────────────┬─────────────────────────────────────┘
                                       │
 ┌─────────────────────────────────────▼─────────────────────────────────────┐
 │                   Anastasia Adaptive Target Backend Router                │
 │       AnaTargetBackend Interface (Tier-1 JIT / OSR Tier-2/3 / AOT Engine) │
 └───────┬─────────────────────────────┬─────────────────────────────┬───────┘
         │                             │                             │
 ┌───────▼──────────────┐   ┌──────────▼───────────┐     ┌───────────▼───────────┐
 │   x86_64 Target      │   │  AArch64 ARM64 Target │     │   OSR & Inlining      │
 │ VEX/EVEX Wide SIMD   │   │  Fixed 32-bit Emitter │     │ Tier-3 Hyper-Unroll   │
 │ System V & Win64 ABI │   │  AAPCS64 Frame File   │     │  Live CPU State Frame │
 └───────┬──────────────┘   └──────────┬───────────┘     └───────────┬───────────┘
         │                             │                             │
 ┌───────▼─────────────────────────────▼─────────────────────────────▼───────────┐
 │                      Anastasia Runtime Memory Subsystem                       │
 │  Branchless TLAB Arena │ VM Guard Page Fault Handler │ Page Write Barrier Remset│
 │  Zero-Copy io_uring    │ Non-Temporal Streaming      │ Frame-Pointer Exception  │
 │  String Interning Pool │ Zero-Linker .rodata Relocs  │ Precise Spilling Allocator│
 └─────────────────────────────────────┬─────────────────────────────────────────┘
                                       │
 ┌─────────────────────────────────────▼─────────────────────────────────────────┐
 │                      Anastasia Output Emission Sinks                          │
 │  MemoryTarget (W^X JIT Memory Pages)  │ PGO AOT ElfEmitter (Relocatable .o)   │
 │  PeEmitter (Windows PE32+ .exe)       │ DWARF 4 Line Info (.debug_line)       │
 └───────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Anastasia Assembly (`.ana`) Syntax & Language Fundamentals

### 3.1 File Structure
An Anastasia Assembly file (`.ana`) contains class definitions (with optional virtual method tables) followed by top-level or method function declarations. Basic blocks are delineated by label declarations (`label_name:`). Structural exception handling uses `.try` and `.catch(ExceptionClass)` blocks.

### 3.2 Type System
Anastasia supports primitive, vector, string, reference, and exception types:
* `i32`: 32-bit signed integer.
* `i64`: 64-bit signed integer.
* `f32`: 32-bit single-precision floating point.
* `f64`: 64-bit double-precision floating point.
* `i32x4`: 128-bit vector packed integer (4 x 32-bit integers).
* `i32x8`: 256-bit vector packed integer (8 x 32-bit integers, AVX2 / VEX).
* `i32x16`: 512-bit vector packed integer (16 x 32-bit integers, AVX-512 / EVEX).
* `ptr`: 64-bit memory address pointer (used for object references, structures, string pointers, and arrays).
* `void`: Empty return type.

### 3.3 Register Model & Stack Spilling
Anastasia supports **unbounded virtual registers** (`v0..vN`). Virtual registers are mapped dynamically to System V AMD64 / Win64 / AAPCS64 physical registers or stack spill slots:

| Virtual Register Range | Storage Kind | x86_64 Location | AArch64 Location | Description |
| :--- | :--- | :--- | :--- | :--- |
| `p0`..`p5` | Parameter | `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9` | `x0`..`x7` | Function parameter registers |
| `v0`..`v4` | Physical Register | `%rax`, `%rdx`, `%r8`, `%r9`, `%r10` | `x0`..`x15` | High-speed physical scratch registers |
| `v5`..`vN` | Stack Spill Slot | `[rbp - 8*N]` | `[x29, #-8*N]` | Automatically allocated stack frame memory |
| `ymm0`..`ymm15` / `zmm0`..`zmm31` | Wide SIMD Vector | `YMM0`..`YMM15` / `ZMM0`..`ZMM31` | `v0`..`v31` | 256-bit and 512-bit vector registers |

---

## 4. Complete Opcode Reference Table

| Instruction Opcode | Operand Syntax | Description |
| :--- | :--- | :--- |
| **Object & Memory Allocation** | | |
| `new-instance` | `dest, ClassName` | Allocate heap object via TLAB or scalar replacement |
| `sink-mem` | `src` | Volatile sink instruction forcing value evaluation (prohibits DCE) |
| **Strings & Read-Only Data** | | |
| `const-string` | `dest, "literal"` | Load interned string pointer into `dest` with parse-time length tracking & `.rodata` AOT relocations |
| **Integer Arithmetic** | | |
| `add-int/32`, `add-int/64` | `dest, src1, src2` | Integer addition (`dest = src1 + src2`) |
| `sub-int/32`, `sub-int/64` | `dest, src1, src2` | Integer subtraction (`dest = src1 - src2`) |
| `mul-int/32`, `mul-int/64` | `dest, src1, src2` | Signed integer multiplication |
| `div-int/32`, `div-int/64` | `dest, src1, src2` | Signed integer division (`idiv`) |
| **Floating-Point & Wide SIMD Vector** | | |
| `add-float/32` | `dest, src1, src2` | Single-precision scalar float addition (`addss`) |
| `add-float/64` | `dest, src1, src2` | Double-precision scalar float addition (`addsd`) |
| `add-vector/i32x4` | `dest, src1, src2` | 128-bit packed 32-bit integer SIMD addition (`paddd`) |
| `add-vector/i32x8` | `dest, src1, src2` | 256-bit packed 32-bit integer AVX2 SIMD addition (`vpaddd ymm`) |
| `add-vector/i32x16` | `dest, src1, src2` | 512-bit packed 32-bit integer AVX-512 SIMD addition (`vpaddd zmm`) |
| `mul-vector/i32x8` | `dest, src1, src2` | 256-bit packed 32-bit integer AVX2 SIMD multiplication (`vpmulld`) |
| `load-vector/256` | `dest, [base]` | Load 256-bit vector from memory (`vmovdqu ymm`) |
| `load-vector/512` | `dest, [base]` | Load 512-bit vector from memory (`vmovdqu zmm`) |
| **Data Movement & Memory** | | |
| `move-const` | `dest, const_val` | Load 64-bit immediate integer into register |
| `move` | `dest, src` | Copy contents of source register to destination |
| `load-mem` | `dest, [base + off]` | Read 64-bit integer from memory address `base + off` |
| `store-mem` | `[base + off], src` | Write 64-bit integer to memory address `base + off` |
| **Async I/O & Hardware Rings** | | |
| `io-submit` | `opcode, fd, addr, len, user_data` | Write SQE into `io_uring` ring and execute syscall |
| `io-poll` | `user_data_out, res_out` | Poll CQE completion from `io_uring` ring buffer |
| **Control Flow & Branches** | | |
| `goto` | `target_label` | Unconditional jump to basic block label |
| `if-eq`, `if-ne`, `if-lt`, `if-ge` | `[hint] src1, src2, target` | Conditional branch instructions with PGO branch hints |
| `return-val` | `src` | Return integer/pointer value in `%rax` / `x0` and execute `ret` |
| `return-void` | *(none)* | Return void and execute `ret` |
| **Bitwise & Hardware Atomics** | | |
| `and-int/64`, `or-int/64`, `xor-int/64` | `dest, src1, src2` | Bitwise operations |
| `shl-int/64`, `shr-int/64`, `ushr-int/64`| `dest, src1, src2` | Shift operations |
| `atomic-cas/64`, `atomic-add/64` | `[base + off], src` | Hardware atomic operations (`lock cmpxchg`, `lock add`) |
| `fence` | *(none)* | Full hardware memory barrier (`mfence` / `sfence`) |

---

## 5. CLI Usage & Verification Matrix

### 5.1 Command Line Interface
```bash
# 1. Execute Anastasia Assembly program in JIT Mode
./build/anastasia_engine program.ana

# 2. Compile Anastasia Assembly program to Relocatable ELF Object File (AOT Mode)
./build/anastasia_engine --aot input.ana output.o

# 3. Run full QA matrix test suite (199 / 199 Tests)
./build/anastasia_engine

# 4. Run high-precision performance benchmarking suite
./build/anastasia_benchmark
```

### 5.2 QA Matrix, Algorithmic & Hardcore Stress Test Execution Output
```text
=======================================================
    Anastasia Bare-Metal Engine QA Test Suite
=======================================================
[Test 1/40] Syscall & Freestanding Memory Operations... PASSED
...
[Test 40/40] Anastasia v7.0 Native Strings, JIT Interning & AOT .rodata Relocations... PASSED

=======================================================
    Anastasia Assembly LeetCode Test Suite (30 Problems)
=======================================================
  Running LC 1: Two Sum... PASSED
  ...
  Running LC 30: Valid Anagram... PASSED
=======================================================
    ALL 30 LEETCODE PROBLEMS EXECUTED SUCCESSFULLY!
=======================================================

=======================================================
    Anastasia Assembly Codeforces 1800+ Suite (30 Problems)
=======================================================
  Running CF 1: Segment Tree Point Update & Range Sum... PASSED
  ...
  Running CF 30: 0-1 BFS Shortest Path... PASSED
=======================================================
    ALL 30 CODEFORCES 1800+ PROBLEMS PASSED CLEANLY!
=======================================================

=======================================================
    Anastasia Assembly Hardcore Test Suite (100 Tests)
=======================================================
  Running HC 1: Heavy-Light Decomposition Path Query... PASSED
  ...
  Running HC 100: High-Precision e Taylor Series Term Step... PASSED
=======================================================
    ALL 100 HARDCORE TESTS PASSED CLEANLY!
=======================================================

=======================================================
    ALL 40 QA MATRIX TESTS SUCCEEDED PERFECTLY!
=======================================================
```

