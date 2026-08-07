# Anastasia Engine v2.0: Technical Specification & Language Reference

## 1. Overview & Philosophy

**Anastasia v2.0** is a master-level, bare-metal, zero-CRT JIT compiler and execution engine for **Extended Smali** (`.ana`). Designed for maximum execution throughput and zero runtime overhead, Anastasia compiles high-level Extended Smali bytecode directly to native x86_64 machine code at runtime without linking against standard C runtimes (`libc`, `libstdc++`), C++ standard libraries, or third-party dependencies.

### Core Architectural Principles
* **Freestanding Bare-Metal Execution**: Operates exclusively under GCC/Clang freestanding flags (`-ffreestanding`, `-nostdlib`, `-nodefaultlibs`, `-fno-exceptions`, `-fno-rtti`) with direct assembly syscall boundaries (`raw_mmap`, `raw_mprotect`, `raw_munmap`, `raw_write`, `raw_exit`).
* **100% Native Freestanding x86_64 Instruction Encoder (`AnaEncoder`)**: Completely eliminates third-party dependencies (AsmJit) by using a native freestanding machine code emitter emitting direct bytes into executable memory.
* **Unbounded Virtual Registers & Stack Spilling (`AnaRegAlloc`)**: Implements dynamic instruction liveness analysis and linear scan register allocation for arbitrary virtual registers (`v0..vN`), automatically spilling excess registers to 16-byte aligned stack slots (`[rbp - 8*N]`).
* **Object Heap Lifecycle & Instantiation (`ObjectHeap` & `new-instance`)**: Provides `.new-instance dest, ClassName` bytecode and a thread-local bump object heap (`ObjectHeap`) with standardized object headers (`vtable_ptr` + `class_id` + `size`).
* **Atomic Multi-Threaded W^X Code Patching & `clflush` Invalidation**: Emits atomic 64-bit store operations during Inline Cache backpatching combined with `clflush` / `mfence` and instruction cache invalidation boundaries.

---

## 2. System Architecture

```
 ┌─────────────────────────────────────────────────────────────┐
 │                    Anastasia Frontend                       │
 │  Smali Lexer ──> SMALI Parser ──> AST ──> Liveness Analysis │
 │  Arena-Based AST Memory (Zero-CRT, Lock-Free Thread-Local)  │
 └────────────────────────────┬────────────────────────────────┘
                              │
 ┌────────────────────────────▼────────────────────────────────┐
 │              Anastasia Bare-Metal JIT Emitter               │
 │  Native x86_64 Encoder (AnaEncoder) (Zero-AsmJit / Zero-CRT) │
 │  RegAlloc Stack Frame Generator & Unbounded Virtual Regs    │
 └────────────────────────────┬────────────────────────────────┘
                              │
 ┌────────────────────────────▼────────────────────────────────┐
 │              Anastasia Runtime & Memory System              │
 │  Raw mmap/mprotect W^X Memory Allocator & Atomic Patching   │
 │  Object Heap Allocator (.new-instance & Monomorphic IC)     │
 └─────────────────────────────────────────────────────────────┘
```

---

## 3. Extended Smali (`.ana`) Syntax & Language Fundamentals

### 3.1 File Structure
An Extended Smali file (`.ana`) contains class definitions (with optional virtual method tables) followed by top-level or method function declarations. Basic blocks are delineated by label declarations (`label_name:`).

### 3.2 Type System
Anastasia supports 5 fundamental primitive and reference types:
* `i32`: 32-bit signed integer.
* `i64`: 64-bit signed integer.
* `f32`: 32-bit single-precision floating point.
* `ptr`: 64-bit memory address pointer (used for object references, structures, and arrays).
* `void`: Empty return type.

### 3.3 Register Model & Stack Spilling
Anastasia supports **unbounded virtual registers** (`v0..vN`). Virtual registers are mapped dynamically to SystemV AMD64 physical registers or stack spill slots:

| Virtual Register Range | Storage Kind | Location | Description |
| :--- | :--- | :--- | :--- |
| `p0`..`p5` | Parameter | `%rdi`, `%rsi`, `%rdx`, `%r8`, `%r9`, `%r10` | Function parameter registers |
| `v0`..`v4` | Physical Register | `%rax`, `%rdx`, `%r8`, `%r9`, `%r10` | High-speed physical scratch registers |
| `v5`..`vN` | Stack Spill Slot | `[rbp - 8*N]` | Automatically allocated stack frame memory |

> [!NOTE]
> `%rcx` and `%r11` are reserved exclusively by the JIT backend for shift count pinning (`%cl`) and lowerer temporary scratch, preventing allocation collisions.

---

## 4. Complete Opcode Reference Table

| Instruction Opcode | Operand Syntax | Description |
| :--- | :--- | :--- |
| **Object Instantiation** | | |
| `new-instance` | `dest, ClassName` | Allocate heap object memory and bind `vtable_ptr` |
| **Arithmetic** | | |
| `add-int/32`, `add-int/64` | `dest, src1, src2` | Integer addition (`dest = src1 + src2`) |
| `sub-int/32`, `sub-int/64` | `dest, src1, src2` | Integer subtraction (`dest = src1 - src2`) |
| `mul-int/32` | `dest, src1, src2` | Signed integer multiplication |
| **Data Movement & Memory** | | |
| `move-const` | `dest, const_val` | Load 64-bit immediate integer into register |
| `move` | `dest, src` | Copy contents of source register to destination |
| `load-mem` | `dest, [base + off]` | Read 64-bit integer from memory address `base + off` |
| `store-mem` | `[base + off], src` | Write 64-bit integer to memory address `base + off` |
| **OOP & VTable Dispatch** | | |
| `bind-vtable` | `dest, vtable_ptr` | Bind VTable pointer address to object instance offset 0 |
| `call-virt` | `dest, obj, slot` | Indirect virtual method call via VTable slot index |
| `call-virt-fast` | `dest, obj, slot` | Monomorphic inline-cached virtual method call |
| **Control Flow** | | |
| `goto` | `target_label` | Unconditional jump to basic block label |
| `if-eq` | `[hint] src1, src2, target` | Jump to `target` if `src1 == src2` |
| `if-ne` | `[hint] src1, src2, target` | Jump to `target` if `src1 != src2` |
| `if-lt` | `[hint] src1, src2, target` | Jump to `target` if `src1 < src2` |
| `if-ge` | `[hint] src1, src2, target` | Jump to `target` if `src1 >= src2` |
| `if-z` | `[hint] src1, target` | Zero-check branch (`test src1, src1`; jump if zero) |
| `if-nz` | `[hint] src1, target` | Non-zero check branch (`test src1, src1`; jump if not zero) |
| `return-val` | `src` | Return integer/pointer value in `%rax` and execute `ret` |
| `return-void` | *(none)* | Return void and execute `ret` |
| **Bitwise & Shifts** | | |
| `and-int/32`, `and-int/64` | `dest, src1, src2` | Bitwise AND (`dest = src1 & src2`) |
| `or-int/32`, `or-int/64` | `dest, src1, src2` | Bitwise OR (`dest = src1 \| src2`) |
| `xor-int/32`, `xor-int/64` | `dest, src1, src2` | Bitwise XOR (`dest = src1 ^ src2`) |
| `shl-int/32`, `shl-int/64` | `dest, src1, src2` | Shift left (`dest = src1 << src2` pinned to `%cl`) |
| `shr-int/32`, `shr-int/64` | `dest, src1, src2` | Arithmetic shift right |
| `ushr-int/32`, `ushr-int/64`| `dest, src1, src2` | Logical (unsigned) shift right |
| `popcount/64` | `dest, src` | Hardware population count (`popcnt dest, src`) |
| `lzcnt/64` | `dest, src` | Hardware leading zero count (`lzcnt dest, src`) |
| **Hardware Atomics** | | |
| `atomic-cas/64` | `[base + off], desired` | Atomic compare-and-swap (`lock cmpxchg`) |
| `atomic-xchg/64` | `[base + off], src` | Atomic exchange (`xchg`) |
| `atomic-add/64` | `[base + off], src` | Lock-free atomic add (`lock add`) |
| `fence` | *(none)* | Full hardware memory barrier (`mfence`) |

---

## 5. Building, Testing, and Verification Matrix

```bash
# 1. Configure freestanding build environment
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 2. Build Anastasia Engine executable binary
cmake --build build

# 3. Execute 13-part QA test matrix and 8-part example execution suite
./build/anastasia_engine
```

### Expected QA Output Verification
```
=======================================================
    Anastasia Bare-Metal Engine QA Test Suite
=======================================================
[Test 1/9] Syscall & Freestanding Memory Operations... PASSED
[Test 2/6] Perfect-Hash Lexer, Arena Allocator & Constant Folding... PASSED
[Test 3/6] AsmJit JIT Lowering & Bare-Metal Execution... PASSED
[Test 4/6] OOP Layout, VTable Dispatch & Monomorphic Inline Cache... PASSED
[Test 5/6] Strict W^X Protection & Instruction Cache Flush... PASSED
[Test 6/9] Dynamic CPU SIMD Routing... PASSED
[Test 7/9] Control Flow, Fused Branches & Fallthrough Optimization... PASSED
[Test 8/9] Bitwise ISA, %cl Shift Pinning & Popcount... PASSED
[Test 9/9] Hardware Lock-Free Atomics & Memory Ordering... PASSED
[Test 10/13] Native Bare-Metal Instruction Encoder (AnaEncoder)... PASSED
[Test 11/13] Unbounded Virtual Registers & Stack Spilling (v0..v15)... PASSED
[Test 12/13] Object Instantiation (new-instance) & Heap Allocation... PASSED
[Test 13/13] Atomic W^X Code Patching & clflush Invalidation... PASSED
---

## 6. Architectural Edge Cases & System V Invariants

### 6.1 System V AMD64 16-Byte Stack Alignment Discipline
The System V AMD64 ABI requires the stack pointer `%rsp` to be aligned to a 16-byte boundary prior to executing any `call` instruction. `AnaRegAlloc` guarantees this invariant by rounding all stack frame allocations up to the nearest 16-byte multiple:
$$\text{stack\_frame\_size} = (\text{spill\_count} \times 8 + 15) \land \sim 15\text{UL}$$
This ensures that helper function invocations (such as `ana_alloc_object`) and virtual method dispatches never crash due to misaligned SIMD vector store operations (`movaps`, `vmovaps`).

### 6.2 Native Instruction Encoder & SIB Byte Resolution (`AnaEncoder`)
x86_64 ModR/M addressing contains an instruction encoding ambiguity when using `%rsp` (register index 4) or `%r12` (extended register index 12 where $12 \bmod 8 = 4$) as a memory base register. To prevent silent encoding corruption:
* `AnaEncoder` automatically emits SIB byte `0x24` (`00 100 100`) whenever the base register index satisfies `(base & 7) == 4`.
* Signed 8-bit displacements (`-128` to `127`) use ModR/M `mod = 01`, while larger offsets use `mod = 10` with 32-bit sign-extended displacements.
* REX prefixes correctly track high registers (`R8`–`R15`) across REX.W, REX.R, REX.X, and REX.B bits.

### 6.3 Heap Memory Recycling & Scope Boundaries (`ObjectHeap`)
`ObjectHeap` provides ultra-fast $O(1)$ bump allocation for live object instances. For batch processing or long-running execution workloads, Anastasia provides region arena reset semantics via `ObjectHeap::instance().reset()`, reclaiming executable heap memory without runtime GC pause overhead.

---

## 7. Anastasia v3.0 Strategic Engineering Roadmap

```
 ┌──────────────────────────────────────────────────────────────────┐
 │               Anastasia v3.0 Unified Core Engine                 │
 │     Smali Parser ──> AST ──> Dual Pipeline Target Router         │
 └─────────────────┬──────────────────────────────┬─────────────────┘
                   │                              │
 ┌─────────────────▼──────────────┐  ┌────────────▼─────────────────┐
 │   Fast JIT Single-Pass Stream  │  │  AOT SSA-IR Optimization Pipeline │
 │  MemoryTarget (W^X mmap Pages) │  │  ObjectFileTarget (ELF / PE .o) │
 └─────────────────┬──────────────┘  └────────────┬─────────────────┘
                   │                              │
 ┌─────────────────▼──────────────────────────────▼─────────────────┐
 │               Multi-Architecture Target Backends                 │
 │     x86_64Backend (AnaEncoder)    │    aarch64Backend (ARM64)      │
 └──────────────────────────────────────────────────────────────────┘
```

### 7.1 AOT Compilation: The JIT/AOT Duality Engine
Refactor `AnaEncoder` and `AnaRegAlloc` to target dual output sinks:
* **MemoryTarget (JIT)**: Direct byte emission to executable `mmap` pages for zero-latency runtime code generation.
* **ObjectFileTarget (AOT)**: `ElfEmitter` (ELF `.o` for Linux/Bare-Metal) and `PeEmitter` (COFF/PE `.obj` for Windows), emitting relocatable `.text` sections with standard relocation symbols (`R_X86_64_PC32`, `R_X86_64_64`). Solves strict W^X kernel lockdown policies for OS kernel and bootloader development.

### 7.2 AArch64 (ARM64) Target Backend Expansion
Abstract backend architecture into an `AnaTargetBackend` interface:
* **`x86_64Backend`**: Current native `AnaEncoder` (ModR/M, REX, SIB byte emission).
* **`aarch64Backend`**: Native ARM64 emitter producing fixed 32-bit instructions, mapping `v0..vN` to ARM64 registers `x0..x30` and NEON vector registers.

### 7.3 Float & SIMD Vector ISA Extension
* **Floating Point Opcodes**: `add-f32/64`, `sub-f32/64`, `mul-f32/64`, `div-f32/64`, `sqrt-f32/64`.
* **Vector Opcodes**: `add-i32x4` (4x32-bit SIMD integer addition), `add-f32x4` (4x32-bit packed float addition).
* **Dual Register File Allocation**: Extend `AnaRegAlloc` to manage separate Integer (`RAX`–`R15`) and Vector (`XMM0`–`XMM15`) register files.

### 7.4 GDB JIT Registration & DWARF Debug Information
* **GDB JIT Registration Interface**: Implement `jit_descriptor_t` and `jit_code_entry_t` protocol to register JIT executable memory addresses with GDB/LLDB debuggers.
* **DWARF Generation (AOT)**: Emit `.debug_info` and `.debug_line` ELF sections to enable source-level debugging in standard toolchains.

### 7.5 OS-Level Freestanding Threading & Synchronization
* **Raw Syscall Threading**: Expose `sys_clone` (syscall 56) and `sys_futex` (syscall 202) for freestanding multi-threaded execution without `libc` or `pthread`.
* **Thread Opcodes**: `.thread-spawn`, `.futex-wait`, `.futex-wake`.

### 7.6 AOT Static Single Assignment Intermediate Representation (SSA-IR)
* **Dual-Pipeline Architecture**: Keep single-pass JIT lowering for high compilation speed, while introducing `AnaSSAIR` for AOT mode.
* **AOT Optimizations**: Loop Invariant Code Motion (LICM), Dead Store Elimination (DSE), and Graph-Coloring Register Allocation.


