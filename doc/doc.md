# Anastasia Engine v3.0: Technical Specification & Ecosystem Reference

## 1. Overview & Philosophy

**Anastasia v3.0** is a master-level, bare-metal, zero-CRT compiler ecosystem and execution engine for **Extended Smali** (`.ana`). Designed for maximum execution throughput, zero runtime overhead, and multi-architecture portability, Anastasia compiles high-level Extended Smali programs directly to native x86_64 and AArch64 (ARM64) machine code at runtime (JIT) or emits relocatable ELF object files (`.o`) for static linking (AOT) without linking against standard C runtimes (`libc`, `libstdc++`), C++ standard libraries, or third-party dependencies.

### Core Architectural Principles
* **Freestanding Bare-Metal Execution**: Operates exclusively under GCC/Clang freestanding flags (`-ffreestanding`, `-nostdlib`, `-nodefaultlibs`, `-fno-exceptions`, `-fno-rtti`) with direct assembly syscall boundaries (`raw_mmap`, `raw_mprotect`, `raw_munmap`, `raw_write`, `raw_read`, `raw_open`, `raw_close`, `raw_clone`, `raw_futex`, `raw_exit`).
* **JIT / AOT Duality Engine**: Dual emission targets via `AnaTargetBackend`: `MemoryTarget` (JIT executable memory) and `ElfEmitter` (relocatable 64-bit ELF object files with symbol tables and relocations).
* **Multi-Architecture Target Portability**: Pluggable backend architecture supporting **x86_64** (`X86_64TargetBackend` / `AnaEncoder`) and **AArch64 / ARM64** (`AArch64TargetBackend` / `AArch64Encoder`).
* **128-bit SIMD Vector & Floating-Point ISA Extensions**: Native SSE2 instruction encodings for single/double precision floats (`f32`/`f64`) and 128-bit SIMD packed integer vector operations (`i32x4`) using `XMM0`–`XMM15` vector registers.
* **Unbounded Virtual Registers & Stack Spilling (`AnaRegAlloc`)**: Implements dynamic instruction liveness analysis and linear scan register allocation for arbitrary virtual registers (`v0..vN`), automatically spilling excess registers to 16-byte aligned stack slots (`[rbp - 8*N]`).
* **GDB JIT Registration & DWARF 4 Line Info**: Standard GDB/LLDB in-memory JIT descriptor interface (`__jit_debug_descriptor`, `__jit_debug_register_code()`) and freestanding DWARF 4 `.debug_line`, `.debug_info`, and `.debug_abbrev` section generator for source-level debugging.
* **Bare-Metal Multithreading & SSA-IR Optimization**: Freestanding kernel thread creation via `raw_clone` (sys_clone, syscall 56), lock-free synchronization via `raw_futex` (sys_futex, syscall 202), and Dominator-Tree SSA IR optimizations (`mem2reg`, LICM, GVN).

---

## 2. System Architecture

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │                        Anastasia Frontend Core                          │
 │   Smali Lexer ──> SMALI Parser ──> AST ──> Liveness & SSA Optimizer    │
 │   Arena-Based AST Memory (Zero-CRT, Lock-Free Thread-Local Arena)       │
 └────────────────────────────────────┬────────────────────────────────────┘
                                      │
 ┌────────────────────────────────────▼────────────────────────────────────┐
 │                  Anastasia Target Backend Router                        │
 │           AnaTargetBackend Interface (JIT / AOT Duality Engine)        │
 └───────────────────┬─────────────────────────────────┬───────────────────┘
                     │                                 │
 ┌───────────────────▼──────────────┐  ┌───────────────▼───────────────────┐
 │      x86_64 Target Backend       │  │     AArch64 (ARM64) Target      │
 │  Native SSE2 & General Encoder   │  │ Fixed 32-bit Machine Code Emitter │
 │  System V AMD64 ABI Stack Frame  │  │  AAPCS64 Frame & Register File    │
 └───────────────────┬──────────────┘  └───────────────┬───────────────────┘
                     │                                 │
 ┌───────────────────▼─────────────────────────────────▼───────────────────┐
 │                     Anastasia Output Emission Sinks                     │
 │  MemoryTarget (W^X JIT Memory Pages) │ ElfEmitter (Relocatable ELF .o)  │
 │  GDB JIT Symbol Descriptor Chain     │ DWARF 4 Line Info (.debug_line)  │
 └─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Extended Smali (`.ana`) Syntax & Language Fundamentals

### 3.1 File Structure
An Extended Smali file (`.ana`) contains class definitions (with optional virtual method tables) followed by top-level or method function declarations. Basic blocks are delineated by label declarations (`label_name:`).

### 3.2 Type System
Anastasia supports primitive, vector, and reference types:
* `i32`: 32-bit signed integer.
* `i64`: 64-bit signed integer.
* `f32`: 32-bit single-precision floating point.
* `f64`: 64-bit double-precision floating point.
* `i32x4`: 128-bit vector packed integer (4 x 32-bit integers).
* `ptr`: 64-bit memory address pointer (used for object references, structures, and arrays).
* `void`: Empty return type.

### 3.3 Register Model & Stack Spilling
Anastasia supports **unbounded virtual registers** (`v0..vN`). Virtual registers are mapped dynamically to System V AMD64 / AAPCS64 physical registers or stack spill slots:

| Virtual Register Range | Storage Kind | x86_64 Location | AArch64 Location | Description |
| :--- | :--- | :--- | :--- | :--- |
| `p0`..`p5` | Parameter | `%rdi`, `%rsi`, `%rdx`, `%r8`, `%r9`, `%r10` | `x0`..`x7` | Function parameter registers |
| `v0`..`v4` | Physical Register | `%rax`, `%rdx`, `%r8`, `%r9`, `%r10` | `x0`..`x15` | High-speed physical scratch registers |
| `v5`..`vN` | Stack Spill Slot | `[rbp - 8*N]` | `[x29, #-8*N]` | Automatically allocated stack frame memory |
| `xmm0`..`xmm15` | SIMD / Vector | `XMM0`..`XMM15` | `v0`..`v15` | Floating-point and 128-bit vector registers |

---

## 4. Complete Opcode Reference Table

| Instruction Opcode | Operand Syntax | Description |
| :--- | :--- | :--- |
| **Object Instantiation** | | |
| `new-instance` | `dest, ClassName` | Allocate heap object memory and bind `vtable_ptr` |
| **Integer Arithmetic** | | |
| `add-int/32`, `add-int/64` | `dest, src1, src2` | Integer addition (`dest = src1 + src2`) |
| `sub-int/32`, `sub-int/64` | `dest, src1, src2` | Integer subtraction (`dest = src1 - src2`) |
| `mul-int/32` | `dest, src1, src2` | Signed integer multiplication |
| **Floating-Point & SIMD Vector** | | |
| `add-float/32` | `dest, src1, src2` | Single-precision scalar float addition (`addss`) |
| `add-float/64` | `dest, src1, src2` | Double-precision scalar float addition (`addsd`) |
| `sub-float/64` | `dest, src1, src2` | Double-precision scalar float subtraction (`subsd`) |
| `mul-float/64` | `dest, src1, src2` | Double-precision scalar float multiplication (`mulsd`) |
| `div-float/64` | `dest, src1, src2` | Double-precision scalar float division (`divsd`) |
| `add-vector/i32x4` | `dest, src1, src2` | 128-bit packed 32-bit integer SIMD addition (`paddd`) |
| `sub-vector/i32x4` | `dest, src1, src2` | 128-bit packed 32-bit integer SIMD subtraction (`psubd`) |
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
| `return-val` | `src` | Return integer/pointer value in `%rax` / `x0` and execute `ret` |
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

## 5. CLI Usage & Verification Matrix

### 5.1 Command Line Interface
```bash
# 1. Execute Extended Smali program in JIT Mode
./build/anastasia_engine program.ana

# 2. Compile Extended Smali program to Relocatable ELF Object File (AOT Mode)
./build/anastasia_engine --aot input.ana output.o

# 3. Run full QA matrix test suite and example execution suite
./build/anastasia_engine
```

### 5.2 QA Matrix Test Execution Output
```
=======================================================
    Anastasia Bare-Metal Engine QA Test Suite
=======================================================
[Test 1/9] Syscall & Freestanding Memory Operations... PASSED
[Test 2/6] Perfect-Hash Lexer, Arena Allocator & Constant Folding... PASSED
[Test 3/6] AsmJit JIT Lowering & Bare-Metal Execution... PASSED
[Test 4/6] OOP Layout, VTable Dispatch & Monomorphic Inline Cache... PASSED
[Test 5/6] Strict W^X Protection & Instruction Cache Flush... PASSED
[Test 6/9] Dynamic CPU SIMD Routing... PASSED (Scalar fallback active)
[Test 7/9] Control Flow, Fused Branches & Fallthrough Optimization... PASSED
[Test 8/9] Bitwise ISA, %cl Shift Pinning & Popcount... PASSED
[Test 9/9] Hardware Lock-Free Atomics & Memory Ordering... PASSED
[Test 10/13] Native Bare-Metal Instruction Encoder (AnaEncoder)... PASSED
[Test 11/13] Unbounded Virtual Registers & Stack Spilling (v0..v15)... PASSED
[Test 12/13] Object Instantiation (new-instance) & Heap Allocation... PASSED
[Test 13/13] Atomic W^X Code Patching & clflush Invalidation... PASSED
[Test 14/14] AOT Relocatable ELF Object File Emitter (ElfEmitter)... PASSED
[Test 15/15] AArch64 Backend & Fixed 32-bit Machine Code Emitter... PASSED
[Test 16/16] Floating-Point & 128-bit SIMD Vector ISA (SSE2)... PASSED
[Test 17/17] GDB JIT Registration & DWARF Line Info... PASSED
[Test 18/18] Bare-Metal Threading (raw_clone), Futex & SSA-IR... PASSED
=======================================================
    ALL 18 QA MATRIX TESTS SUCCEEDED PERFECTLY!
=======================================================
```

---

## 6. Architectural Edge Cases & System V Invariants

### 6.1 System V AMD64 16-Byte Stack Alignment Discipline
The System V AMD64 ABI requires the stack pointer `%rsp` to be aligned to a 16-byte boundary prior to executing any `call` instruction. `AnaRegAlloc` guarantees this invariant by rounding all stack frame allocations up to the nearest 16-byte multiple:
$$\text{stack\_frame\_size} = (\text{spill\_count} \times 8 + 15) \land \sim 15\text{UL}$$
Furthermore, the bare-metal entry point `_start` aligns `%rsp` to 16 bytes (`and $-16, %rsp`) before delegating control to `_start_c`, preventing alignment faults on vector operations (`movaps`, `movdqa`).

### 6.2 Native Instruction Encoder & SIB Byte Resolution (`AnaEncoder`)
x86_64 ModR/M addressing contains an instruction encoding ambiguity when using `%rsp` (register index 4) or `%r12` (extended register index 12 where $12 \bmod 8 = 4$) as a memory base register. To prevent silent encoding corruption:
* `AnaEncoder` automatically emits SIB byte `0x24` (`00 100 100`) whenever the base register index satisfies `(base & 7) == 4`.
* Signed 8-bit displacements (`-128` to `127`) use ModR/M `mod = 01`, while larger offsets use `mod = 10` with 32-bit sign-extended displacements.
* REX prefixes correctly track high registers (`R8`–`R15`) across REX.W, REX.R, REX.X, and REX.B bits.

### 6.3 Heap Memory Recycling & Scope Boundaries (`ObjectHeap`)
`ObjectHeap` provides ultra-fast $O(1)$ bump allocation for live object instances. For batch processing or long-running execution workloads, Anastasia provides region arena reset semantics via `ObjectHeap::instance().reset()`, reclaiming executable heap memory without runtime GC pause overhead.
