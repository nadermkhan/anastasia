# Anastasia Engine v7.1

<p align="center">
  <img src="https://img.shields.io/badge/Language-Extended%20Smali%20%7C%20C%2B%2B20-blue.svg" alt="Language">
  <img src="https://img.shields.io/badge/Dependencies-Zero%20%28100%25%20Freestanding%20Zero--CRT%29-brightgreen.svg" alt="Dependencies">
  <img src="https://img.shields.io/badge/Architecture-x86__64%20%7C%20AArch64%20%28ARM64%29-orange.svg" alt="Architecture">
  <img src="https://img.shields.io/badge/SIMD-AVX2%20%7C%20AVX--512%20%7C%20VEX%2FEVEX-purple.svg" alt="SIMD">
  <img src="https://img.shields.io/badge/Tests-99%2F99%20Passed%20%28100%25%29-success.svg" alt="Tests">
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License">
</p>

> **The World's Fastest Embeddable, Bare-Metal, Zero-CRT JIT & AOT Compiler Engine for Extended Smali (`.ana`)**

**Anastasia** is a state-of-the-art, high-throughput, bare-metal compiler ecosystem and adaptive execution runtime engineered from the ground up to eliminate dynamic runtime overhead, third-party libraries, and standard C runtime (`libc` / `libstdc++`) dependencies. Compiling **Extended Smali** (`.ana`) instructions directly into native machine code, Anastasia targets x86_64 (AVX2 / AVX-512 VEX & EVEX vector encodings) and AArch64 (ARM64) at runtime (**JIT**) or emits standalone relocatable ELF object files (`.o`) and PE32+ executables (`.exe`) (**AOT**).

Designed for ultra-low latency system software, high-frequency data pipelines, real-time analytics, and high-performance computing, Anastasia pairs raw assembly execution speed with advanced SSA optimizations, zero-copy `io_uring` hardware async I/O, non-temporal RAM streaming, and trap-free garbage collection.

---

## ⚡ Key Architectural Highlights

* 🛡️ **100% Freestanding Zero-CRT Philosophy**: Operates strictly under `-ffreestanding`, `-nostdlib`, `-nodefaultlibs`, `-fno-exceptions`, `-fno-rtti` with zero third-party dependencies (`AnaEncoder`). Executes directly on Linux/Win32 kernel syscall boundaries (`raw_mmap`, `raw_mprotect`, `raw_write`, `raw_clone`, `raw_futex`, `raw_mbind`, `raw_io_uring`).
* 🔤 **Native String Literals & Zero-Linker `.rodata` Emission**: Native `const-string` support with parse-time zero-copy length tracking (eliminating `strlen()` overhead). Features a JIT read-only interned string pool and an AOT freestanding ELF linker (`ElfEmitter`) capable of emitting `.rodata` sections and patching RIP-relative relocations (`R_X86_64_PC32` / `R_AARCH64_ADR_PREL_PG_HI21`) without needing `ld` or `link.exe`.
* 🚀 **Multi-Architecture Machine Code Encoders**: Native x86_64 instruction encoder (`AnaEncoder`) featuring VEX (256-bit AVX2 `YMM`) and EVEX (512-bit AVX-512 `ZMM`) byte-packing, paired with a fixed 32-bit AArch64 (ARM64) machine code backend (`AArch64Encoder`).
* 🧮 **SSA Optimization & Autovectorization Suite**:
  * **SSA Counted-Loop Autovectorizer**: Transforms scalar loops into 256-bit or 512-bit packed SIMD vector operations.
  * **Non-Temporal Store Streaming**: Detects sequential writes (>128 elements), emitting non-temporal stores (`vmovntdq` + `sfence`) to bypass L1/L2/L3 cache pollution.
  * **Adaptive D-Cache Software Prefetching**: Dynamically injects `prefetcht0` instructions ahead of memory load pointers.
  * **Escape Analysis & Scalar Replacement**: Allocates non-escaping objects directly to virtual registers and stack slots (**0 heap allocations**).
  * **Speculative Inlining & On-Stack Replacement (OSR)**: Monomorphic call site inlining and loop safe-point OSR tiering (`RAX`–`R15` register capture).
* 🌐 **Zero-Copy Hardware Async I/O (`io_uring`)**: Submission Queue (SQ) and Completion Queue (CQ) ring buffers managed directly via kernel `raw_mmap`. The `io-submit` instruction lowers to ring buffer writes and `sys_io_uring_enter` with zero user-space copying.
* 📦 **Branchless TLAB Allocation & VM Guard Pages**: Fast-path Thread-Local Allocation Buffer (`tlab_allocate`) performing branchless bump-pointer allocations (`mov`, `lea`, `mov`). Overruns trigger a freestanding `SIGSEGV` fault handler to allocate new 64 KB TLAB slabs transparently.
* 💯 **100% Verification Coverage**: Passes 99/99 comprehensive engine tests, including 40 Core Engine QA Matrix Tests, 30 LeetCode Problem Solutions, and 30 Codeforces 1800+ Rated Competitive Programming Algorithms.

---

## 🏛️ System Architecture

```
 ┌───────────────────────────────────────────────────────────────────────────┐
 │                         Anastasia Frontend Core                           │
 │  Smali Lexer ──> SMALI Parser ──> AST ──> SSA Optimizer & Vectorizer     │
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

## 🛠️ Quick Start & Build System

Anastasia comes equipped with an automated build system for building, testing, and benchmarking across Linux and Windows environments.

### Prerequisites
* **C++ Compiler**: GCC 10+ or Clang 12+ (supporting C++20 standard).
* **Build Tools**: CMake 3.20+ and GNU Make / Ninja.

### Building & Running
```bash
# 1. Clone the repository
git clone https://github.com/nadermkhan/anastasia.git
cd anastasia

# 2. Build the project using the build script
./build.sh

# 3. Run the full 99-test QA suite
./build.sh --test

# 4. Run the high-precision benchmark suite
./build.sh --bench
```

### CLI Command Options
```bash
# Execute Extended Smali program in JIT Mode
./build/anastasia_engine program.ana

# Compile Extended Smali program to Relocatable ELF Object File (AOT Mode)
./build/anastasia_engine --aot input.ana output.o

# Perform clean build and run tests
./build.sh --clean --test
```

---

## 📜 Extended Smali (`.ana`) Code Examples

### 1. High-Performance Loop with Branch Hints & Volatile Sink
```smali
.fn compute_factorial(p0: i64) -> i64
    .registers 3 local
    move-const v0, 1
    move-const v1, 1

loop_start:
    if-ge likely v1, p0, loop_end
    mul-int/32 v0, v0, v1
    add-int/64 v1, v1, 1
    goto loop_start

loop_end:
    sink-mem v0
    return-val v0
.end_fn
```

### 2. Native String Literals & String Interning
```smali
.fn get_engine_status() -> ptr
    .registers 1 local
    const-string v0, "Anastasia v7.1 Bare-Metal Engine Active"
    return-val v0
.end_fn
```

### 3. Lock-Free Hardware Atomic Counter
```smali
.fn increment_atomic_counter(p0: ptr, p1: i64) -> i64
    .registers 1 local
    atomic-add/64 [p0 + 0], p1
    fence
    load-mem v0, [p0 + 0]
    return-val v0
.end_fn
```

---

## 🔬 Benchmark & Test Matrix

Anastasia includes a comprehensive 99-test verification matrix covering bare-metal engine subsystems, LeetCode algorithms, and Codeforces 1800+ competitive programming solutions:

| Test Suite | Total Tests | Pass Rate | Coverage |
|---|---|---|---|
| **Core Engine QA Matrix** | 40 | **100% (40/40)** | Syscalls, TLAB, OSR, GC, VEX/EVEX, io_uring, AOT ELF/PE, Native Strings |
| **LeetCode Algorithm Suite** | 30 | **100% (30/30)** | Two Sum, Kadane, Boyer-Moore, Binary Search, Palindromes, Fast Power |
| **Codeforces 1800+ Suite** | 30 | **100% (30/30)** | Segment Tree, LIS, DSU, Dijkstra, Matrix Exp, KMP, Z-Algo, FWHT, 2-SAT |
| **Total Ecosystem Matrix** | **99** | **100% (99/99)** | **Complete JIT & AOT Functional Verification** |

---

## 📑 Technical Documentation & Guides

For deep architectural specifications, language grammar, and LLM AI prompt integration, refer to the project documentation:

* **[Engine Specification (`doc/doc.md`)](doc/doc.md)**: In-depth technical specification, register spilling model, opcode dictionary, and ISA lowering pipelines.
* **[AI LLM Instruction Manual (`llm_instructions_learn_anastasia_for_ai.md`)](llm_instructions_learn_anastasia_for_ai.md)**: Zero-hallucination EBNF grammar rules, type system guidelines, and LLM prompt specifications for generating `.ana` code.

---

## 📄 License

This project is open-source software licensed under the **MIT License**.

```
Copyright (c) 2026 Nader Mahbub Khan
```
