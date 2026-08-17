# LLM AI Instruction Manual & System Prompt: Anastasia Engine (.ana) v7.1

> **Complete Teaching Context & Grammar Specification for Large Language Models (LLMs)**  
> **Target Engine**: Anastasia v7.1 Freestanding Bare-Metal JIT/AOT Compiler  
> **File Extension**: `.ana`  
> **Objective**: Equip LLMs with complete, zero-hallucination knowledge to parse, validate, optimize, and generate 100% syntactically valid Anastasia Assembly code and system integrations.

---

## 1. Zero-Hallucination Directives & System Invariants

When reading, analyzing, or emitting code for Anastasia Engine, LLMs MUST strictly enforce these fundamental invariants:

1. **100% Bare-Metal Freestanding Environment (Zero-CRT / Zero-glibc)**:
   - Anastasia programs execute without `libc`, `libstdc++`, or standard C runtime initialization.
   - **FORBIDDEN**: `#include`, `import`, `printf`, `malloc`, `free`, `std::cout`, `exit`.
   - **REQUIRED**: All I/O and memory allocations must use direct freestanding syscalls (`sys::raw_write`, `sys::raw_mmap`) or Anastasia ObjectHeap TLAB bump allocations.
2. **NO Synthetic Assembly Opcodes**:
   - Never generate raw x86-64 or ARM assembly instructions directly (e.g., `movq`, `push`, `pop`, `call`, `jmp`, `ldr`, `str`).
   - Use **ONLY** valid Anastasia Assembly opcodes defined in Section 4.
3. **Destination-First 3-Operand Register Convention**:
   - Standard arithmetic format: `opcode dest, src1, src2` (e.g., `add-int/64 v0, p0, v1`).
   - Standard unary/copy format: `opcode dest, src` (e.g., `move v1, p0`).
4. **Mandatory Function Framing**:
   - Every function definition MUST start with `.fn function_name(param1: type1, param2: type2) -> return_type`.
   - Every function MUST declare `.registers N local` on the line immediately following `.fn`.
   - Every function MUST close with `.end_fn`.
5. **Strict Explicit Returns**:
   - Non-void functions MUST terminate all basic block exit paths with `return-val register`.
   - Void functions MUST terminate all basic block exit paths with `return-void`.
6. **Native Read-Only String Literals (`const-string`)**:
   - Use `const-string dest, "literal"` to load string pointers. Anastasia tracks lengths at compile time (0-copy `strlen`), interned into read-only memory in JIT mode, and emitted as RIP-relative `.rodata` relocations in freestanding AOT mode.

---

## 2. Register Architecture & Type System

### 2.1 Complete Type System
| Type Keyword | Description | Byte Size | Memory / Register Layout |
|---|---|---|---|
| `i32` | 32-bit signed integer | 4 bytes | General Purpose Register (`%eax`, `%ebx`, etc.) |
| `i64` | 64-bit signed integer | 8 bytes | General Purpose Register (`%rax`, `%rbx`, etc.) |
| `f32` | 32-bit single-precision float | 4 bytes | SSE2 XMM Vector Register (`%xmm0`..`%xmm15`) |
| `f64` | 64-bit double-precision float | 8 bytes | SSE2 XMM Vector Register (`%xmm0`..`%xmm15`) |
| `i32x4` | 128-bit SIMD vector (4 x i32) | 16 bytes | SSE2 XMM Register (`paddd`) |
| `i32x8` | 256-bit AVX2 vector (8 x i32) | 32 bytes | AVX2 YMM Register (`vpaddd ymm`) |
| `i32x16` | 512-bit AVX-512 vector (16 x i32) | 64 bytes | AVX-512 ZMM Register (`vpaddd zmm`) |
| `ptr` | 64-bit raw pointer / object / string | 8 bytes | General Purpose Pointer (`%rdi`, `%rsi`, etc.) |
| `void` | Empty return type | 0 bytes | N/A |

### 2.2 Parameter & Virtual Local Register Calling Convention
- **Parameter Registers (`p0` .. `p5`)**:
  - Bound to incoming function arguments in order (`p0` = 1st parameter, `p1` = 2nd parameter, etc.).
  - Read-only parameter bindings.
- **Local Virtual Registers (`v0` .. `vN`)**:
  - Unbounded virtual registers (`v0`, `v1`, `v2`, `v3`, ..., `v999`).
  - Graph-coloring SSA register allocation maps `v0`..`v4` to hardware registers (`%rax`, `%rcx`, `%rdx`, `%rsi`, `%rdi`).
  - Virtual registers `v5`..`vN` spill automatically to isolated stack frame slots `[rbp - 8*N]`.

---

## 3. Formal EBNF Grammar Specification

```ebnf
Program         ::= (ClassDef | FunctionDef)* ;

ClassDef        ::= ".class" Identifier ( ".vtable" Identifier* ".end_vtable" )? ".end_class" ;

FunctionDef     ::= ".fn" Identifier "(" ParamList? ")" "->" Type
                    ".registers" Integer "local"
                    BasicBlock*
                    ".end_fn" ;

ParamList       ::= Param ("," Param)* ;
Param           ::= Identifier ":" Type ;

BasicBlock      ::= (Identifier ":")? Instruction* ;

Instruction     ::= Opcode OperandList ;

OperandList     ::= (Operand ("," Operand)*)? ;
Operand         ::= Register | Immediate | StringLiteral | MemoryOperand | LabelRef ;

Register        ::= ParamReg | LocalReg ;
ParamReg        ::= "p" [0-9]+ ;
LocalReg        ::= "v" [0-9]+ ;

MemoryOperand   ::= "[" Register "+" Integer "]" ;
LabelRef        ::= Identifier ;
BranchHint      ::= "likely" | "unlikely" ;
```

---

## 4. Complete Valid Anastasia Assembly Opcode Dictionary

### 4.1 Data Movement & Memory Ops
- `move-const dest, constant`: Load immediate 64-bit integer into `dest`. (e.g. `move-const v0, 42`)
- `const-string dest, "text"`: Load interned string pointer into `dest`. (e.g. `const-string v0, "Hello"`)
- `move dest, src`: Copy register `src` to `dest`. (e.g. `move v1, p0`)
- `load-mem dest, [base + offset]`: Read 64-bit word from `base + offset`. (e.g. `load-mem v0, [p0 + 8]`)
- `store-mem [base + offset], src`: Write 64-bit word `src` to `base + offset`. (e.g. `store-mem [p0 + 16], v1`)

### 4.2 Integer Arithmetic
- `add-int/32 dest, src1, src2` | `add-int/64 dest, src1, src2`: Addition (`dest = src1 + src2`).
- `sub-int/32 dest, src1, src2` | `sub-int/64 dest, src1, src2`: Subtraction (`dest = src1 - src2`).
- `mul-int/32 dest, src1, src2` | `mul-int/64 dest, src1, src2`: Multiplication (`dest = src1 * src2`).
- `div-int/32 dest, src1, src2` | `div-int/64 dest, src1, src2`: Division (`dest = src1 / src2`).

### 4.3 Floating-Point & Wide SIMD Vectors
- `add-float/32 dest, src1, src2`: 32-bit single-precision float addition (`addss`).
- `add-float/64 dest, src1, src2`: 64-bit double-precision float addition (`addsd`).
- `sub-float/64 dest, src1, src2`: 64-bit double-precision float subtraction (`subsd`).
- `mul-float/64 dest, src1, src2`: 64-bit double-precision float multiplication (`mulsd`).
- `div-float/64 dest, src1, src2`: 64-bit double-precision float division (`divsd`).
- `add-vector/i32x4 dest, src1, src2`: 128-bit SSE2 packed SIMD addition (`paddd`).
- `add-vector/i32x8 dest, src1, src2`: 256-bit AVX2 packed SIMD addition (`vpaddd ymm`).
- `add-vector/i32x16 dest, src1, src2`: 512-bit AVX-512 packed SIMD addition (`vpaddd zmm`).

### 4.4 Bitwise & Shift Operations
- `and-int/32 dest, src1, src2` | `and-int/64 dest, src1, src2`: Bitwise AND.
- `or-int/32 dest, src1, src2`  | `or-int/64 dest, src1, src2`: Bitwise OR.
- `xor-int/32 dest, src1, src2` | `xor-int/64 dest, src1, src2`: Bitwise XOR.
- `shl-int/32 dest, src1, src2` | `shl-int/64 dest, src1, src2`: Logical shift left.
- `shr-int/32 dest, src1, src2` | `shr-int/64 dest, src1, src2`: Arithmetic shift right.
- `ushr-int/32 dest, src1, src2` | `ushr-int/64 dest, src1, src2`: Logical shift right.
- `popcount/64 dest, src`: Population count (number of set bits).
- `lzcnt/64 dest, src`: Leading zero count.

### 4.5 Control Flow & Speculative Branching
- `goto label_name`: Unconditional jump to `label_name`.
- `if-eq [hint] src1, src2, label`: Jump to `label` if `src1 == src2`. Optional hint: `likely` or `unlikely`.
- `if-ne [hint] src1, src2, label`: Jump to `label` if `src1 != src2`.
- `if-lt [hint] src1, src2, label`: Jump to `label` if `src1 < src2`.
- `if-ge [hint] src1, src2, label`: Jump to `label` if `src1 >= src2`.
- `if-z [hint] src, label`: Jump to `label` if `src == 0`.
- `if-nz [hint] src, label`: Jump to `label` if `src != 0`.
- `return-val src`: Return value in `src`.
- `return-void`: Return from void function.

### 4.6 Object Instantiation & Virtual Dispatch
- `new-instance dest, ClassName`: Allocate object instance of `ClassName` via TLAB bump pointer.
- `bind-vtable dest, vtable_symbol`: Attach VTable pointer to object header offset 0.
- `call-virt dest, obj_ptr, slot_index`: Indirect VTable virtual method call.
- `call-virt-fast dest, obj_ptr, slot_index`: Monomorphic inline-cached fast virtual call.

### 4.7 Hardware Atomics & Memory Barriers
- `atomic-cas/64 [base + offset], desired`: Lock-free compare-and-swap (`lock cmpxchg`).
- `atomic-xchg/64 [base + offset], src`: Atomic exchange (`xchg`).
- `atomic-add/64 [base + offset], src`: Lock-free atomic addition (`lock add`).
- `fence`: Hardware memory barrier (`mfence`).

---

## 5. Architectural Deep-Dive: Memory, Fault Tolerance & Optimizations

### 5.1 Freestanding Syscall Layer (`sys_raw`)
Anastasia implements direct ring-0 kernel system calls without relying on `glibc` or C runtime wrapper overhead:
- `raw_write(int fd, const void* buf, size_t count)`: Low-latency unbuffered system write (`syscall 1`).
- `raw_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off)`: Direct page allocation (`syscall 9`).
- `raw_munmap(void* addr, size_t len)`: Direct page release (`syscall 11`).
- `raw_futex(int* uaddr, int op, int val)`: Fast user-space lock/wake primitive (`syscall 202`).
- `raw_clone(...)`: Native Linux thread creation (`syscall 56`).

### 5.2 Thread-Local Allocation Buffer (TLAB) & Heap Memory Layout
- **Object Alignment**: All object allocations are 64-byte aligned to prevent L1/L2 cache line false sharing.
- **Header Spec**: Every object allocated via `new-instance` has a **16-byte object header** (8-byte VTable pointer + 4-byte GC mark-word + 4-byte Class ID).
- **Fast-Path Bump Allocation**: `tlab_allocate` increments a thread-local pointer in user space (~71 ns/alloc), falling back to kernel `mmap` slabs only when the 2MB thread slab is exhausted.

### 5.3 Freestanding Crash Interceptor (`AnaTrapHandler`)
Anastasia provides native signal interception and register diagnostics without CRT signal handling:
- **Signals Intercepted**: `SIGSEGV` (Memory fault), `SIGFPE` (Arithmetic fault), `SIGILL` (Illegal instruction), `SIGBUS` (Unaligned access).
- **Signal Stack Isolation**: Registers dedicated signal stack via `sigaltstack` to prevent stack overflow corruption during traps.
- **Diagnostic Register Dump**: Dumps full CPU state (`RIP`, `RSP`, `RAX`, `RBX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`..`R15`) on hardware trap.

### 5.4 Optimizer Passes & On-Stack Replacement (OSR)
- **SSA Loop Induction Reduction**: Converts linear accumulation loops $S = \sum_{i=0}^N i$ into $O(1)$ constant formulas at compilation time.
- **Dead-Code Elimination (DCE)**: Prunes unused SSA instructions and unreachable basic blocks.
- **On-Stack Replacement (OSR)**: Dynamically replaces running interpreter loops with compiled machine code frames without losing local variable state.

---

## 6. Verified Canonical Code Examples

### Example 1: High-Performance Scalar Loop
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
    return-val v0
.end_fn
```

### Example 2: Object Instantiation & Field Mutation
```smali
.class Widget
    .vtable
        Widget_render
    .end_vtable
.end_class

.fn create_widget_instance(p0: i64) -> i64
    .registers 2 local
    new-instance v0, Widget
    store-mem [v0 + 8], 777
    load-mem v1, [v0 + 8]
    add-int/64 v1, v1, p0
    return-val v1
.end_fn
```

### Example 3: Lock-Free Atomic Counter
```smali
.fn atomic_increment(p0: ptr, p1: i64) -> i64
    .registers 1 local
    atomic-add/64 [p0 + 0], p1
    fence
    load-mem v0, [p0 + 0]
    return-val v0
.end_fn
```

### Example 4: Native String Literals
```smali
.fn get_engine_banner() -> ptr
    .registers 1 local
    const-string v0, "Anastasia Engine v7.1 Terabyte-Compute"
    return-val v0
.end_fn
```

---

## 7. LLM Self-Audit Checklist

Before outputting Anastasia Assembly code, perform this mandatory verification:

1. [ ] Does every function declare `.registers N local` immediately after `.fn`?
2. [ ] Are function arguments bound in exact order to `p0`, `p1`, `p2`...?
3. [ ] Are all instructions using 3-operand destination-first format (`opcode dest, src1, src2`)?
4. [ ] Are all branch target labels properly declared as `name:` and referenced accurately?
5. [ ] Does every exit path terminate with `return-val` (for non-void) or `return-void` (for void)?
6. [ ] Are all opcodes 100% present in Section 4? (Zero synthetic or hallucinated instructions allowed).

---

## 8. Anastasia AOT Backend Engine Bug Diagnosis & Resolution Reference

When debugging Anastasia AOT compilation, LLMs should reference these 5 foundational backend fixes:

1. **Missing Opcode Lowering**:
   - *Symptom*: Output binary exits code `0` immediately.
   - *Root Cause*: Instruction lowerer (`ana_lowerer.cpp`) missing opcode handling (`call-extern`, `load-fn-ptr`, `load-mem`, `store-mem`, `mul/div/xor`, `move`, `if-z/nz`, `goto`, `new-instance`). Unhandled instructions fell through to `default: break;`.
   - *Fix*: Implemented System V AMD64 ABI machine lowerers & `R_X86_64_PLT32` / `R_X86_64_PC32` ELF relocations.
2. **Memory-Ref Parameter Register Allocation**:
   - *Symptom*: SEGFAULT at `mov 0x8(%rax), %rbx`.
   - *Root Cause*: `check_p` in `ana_regalloc.cpp` skipped `OperandKind::MEM_OFFSET` (`[p1 + 8]`), causing `p1` to fall back to callee-saved slot `R12`.
   - *Fix*: Inspected `MEM_OFFSET` base registers in `check_p` and fixed AST linkage.
3. **Sub-Call Parameter Preservation**:
   - *Symptom*: External call crashes (e.g., GLib assertion `argc == 0 || argv != NULL`).
   - *Root Cause*: `CALL_EXTERN` missing from `has_call` check in `AnaRegAlloc`, causing outgoing calls to overwrite incoming parameter registers `RDI` and `RSI`.
   - *Fix*: Added `CALL_EXTERN` to `has_call` check, forcing parameter registers to be preserved in dedicated stack slots (`-0x30(rbp)`, `-0x38(rbp)`).
4. **Parser Token Hash Skipping**:
   - *Symptom*: Assembly instructions silently ignored during parsing.
   - *Root Cause*: Missing keyword token mappings for `call-extern` and `load-fn-ptr`.
   - *Fix*: Added token definitions and AST parsing nodes in `ana_ast.h`, `ana_lexer.cpp`, and `ana_parser.cpp`.
5. **ELF Relative Call PLT Relocation Addend**:
   - *Symptom*: Misaligned PLT call addresses.
   - *Root Cause*: x86_64 `call rel32` (`0xE8`) displacement computes relative to instruction end (4 bytes after opcode), missing `-4` addend.
   - *Fix*: Added `-4` addend adjustment to `call_rel32_symbol` and `lea_reg_symbol_rip` in `ana_encoder.cpp`.
