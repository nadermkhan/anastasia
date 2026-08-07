# LLM AI Instruction Manual: Extended Smali (.ana) for Anastasia v3.0

> **System Prompt & Grammar Specification for Large Language Models (LLMs)**  
> **Target System**: Anastasia v3.0 Bare-Metal Compiler Engine  
> **File Extension**: `.ana`  
> **Objective**: Enable LLMs to parse, validate, and emit 100% syntactically correct, non-hallucinated Extended Smali assembly programs.

---

## 1. Zero-Hallucination Directives & Core Invariants

When generating code for Anastasia, you MUST adhere to the following strict rules. Failure to obey any rule will cause a compilation error in the Anastasia Bare-Metal Engine.

1. **NO Dynamic Imports or External Standard Libraries**:
   - Never generate `#include`, `import`, `printf`, `malloc`, or `std::cout`. Anastasia is a bare-metal, zero-CRT environment.
2. **NO Synthetic Assembly Opcodes**:
   - Never generate raw x86_64 or ARM assembly instructions (e.g. `movq`, `push`, `pop`, `call`, `jmp`, `ldr`, `str`).
   - Use **ONLY** valid Extended Smali opcodes defined in Section 4.
3. **Destination-First Operand Ordering**:
   - All instructions follow 3-operand destination-first format: `opcode dest, src1, src2` or 2-operand `opcode dest, src`.
4. **Mandatory Function Framing**:
   - Every function MUST start with `.fn function_name(params) -> return_type`.
   - Every function MUST define `.registers N local` immediately after `.fn`.
   - Every function MUST end with `.end_fn`.
5. **Explicit Returns**:
   - Non-void functions MUST terminate paths with `return-val register`.
   - Void functions MUST terminate paths with `return-void`.

---

## 2. Type System & Register Architecture

### 2.1 Primitive and Vector Types
| Type | Description | Size |
| :--- | :--- | :--- |
| `i32` | 32-bit signed integer | 4 bytes |
| `i64` | 64-bit signed integer | 8 bytes |
| `f32` | 32-bit single-precision float | 4 bytes |
| `f64` | 64-bit double-precision float | 8 bytes |
| `i32x4` | 128-bit SIMD vector (4 x 32-bit integers) | 16 bytes |
| `ptr` | 64-bit raw pointer / object reference | 8 bytes |
| `void` | Empty return type | 0 bytes |

### 2.2 Register Naming Rules
- **Parameter Registers (`p0` .. `p5`)**:
  - Represent function arguments passed in order (`p0` = 1st arg, `p1` = 2nd arg, etc.).
  - Read-only parameter bindings.
- **Local Virtual Registers (`v0` .. `vN`)**:
  - Unbounded local registers (`v0`, `v1`, `v2`, `v3`, ..., `v999`).
  - Registers `v0`..`v4` map to physical registers; `v5`..`vN` spill automatically to stack memory slots `[rbp - 8*N]`.

---

## 3. Program Layout & Grammar (EBNF)

```ebnf
Program ::= (ClassDef | FunctionDef)* ;

ClassDef ::= ".class" Identifier ( ".vtable" Identifier* ".end_vtable" )? ".end_class" ;

FunctionDef ::= ".fn" Identifier "(" ParamList? ")" "->" Type
                ".registers" Integer "local"
                BasicBlock*
                ".end_fn" ;

ParamList ::= Param ("," Param)* ;
Param     ::= Identifier ":" Type ;

BasicBlock ::= (Identifier ":")? Instruction* ;

Instruction ::= Opcode OperandList ;
```

---

## 4. Complete Valid Opcode Dictionary

### 4.1 Data Movement & Memory
- `move-const dest, constant`: Loads 64-bit immediate integer into `dest`.  
  *Example*: `move-const v0, 42`
- `move dest, src`: Copies register `src` to `dest`.  
  *Example*: `move v1, p0`
- `load-mem dest, [base + offset]`: Reads 64-bit word from `base + offset`.  
  *Example*: `load-mem v0, [p0 + 8]`
- `store-mem [base + offset], src`: Writes 64-bit word `src` to `base + offset`.  
  *Example*: `store-mem [p0 + 16], v1`

### 4.2 Integer Arithmetic
- `add-int/32 dest, src1, src2`: 32-bit addition (`dest = src1 + src2`).  
- `add-int/64 dest, src1, src2`: 64-bit addition (`dest = src1 + src2`).  
- `sub-int/32 dest, src1, src2`: 32-bit subtraction (`dest = src1 - src2`).  
- `sub-int/64 dest, src1, src2`: 64-bit subtraction (`dest = src1 - src2`).  
- `mul-int/32 dest, src1, src2`: 32-bit multiplication (`dest = src1 * src2`).  

### 4.3 Floating-Point & 128-bit SIMD Vector
- `add-float/32 dest, src1, src2`: 32-bit single-precision float addition (`addss`).  
- `add-float/64 dest, src1, src2`: 64-bit double-precision float addition (`addsd`).  
- `sub-float/64 dest, src1, src2`: 64-bit double-precision float subtraction (`subsd`).  
- `mul-float/64 dest, src1, src2`: 64-bit double-precision float multiplication (`mulsd`).  
- `div-float/64 dest, src1, src2`: 64-bit double-precision float division (`divsd`).  
- `add-vector/i32x4 dest, src1, src2`: 128-bit packed SIMD integer addition (`paddd`).  
- `sub-vector/i32x4 dest, src1, src2`: 128-bit packed SIMD integer subtraction (`psubd`).  

### 4.4 Bitwise & Shifts
- `and-int/32 dest, src1, src2` | `and-int/64 dest, src1, src2`: Bitwise AND.  
- `or-int/32 dest, src1, src2`  | `or-int/64 dest, src1, src2`: Bitwise OR.  
- `xor-int/32 dest, src1, src2` | `xor-int/64 dest, src1, src2`: Bitwise XOR.  
- `shl-int/32 dest, src1, src2` | `shl-int/64 dest, src1, src2`: Shift left (`dest = src1 << src2`).  
- `shr-int/32 dest, src1, src2` | `shr-int/64 dest, src1, src2`: Arithmetic shift right.  
- `ushr-int/32 dest, src1, src2` | `ushr-int/64 dest, src1, src2`: Logical shift right.  
- `popcount/64 dest, src`: Population count (number of set bits).  
- `lzcnt/64 dest, src`: Leading zero count.  

### 4.5 Control Flow & Branching
- `goto label_name`: Unconditional branch.  
- `if-eq [hint] src1, src2, label_name`: Branch if `src1 == src2`. Optional hints: `likely` or `unlikely`.  
  *Example*: `if-eq likely v0, p0, loop_end`
- `if-ne [hint] src1, src2, label_name`: Branch if `src1 != src2`.  
- `if-lt [hint] src1, src2, label_name`: Branch if `src1 < src2`.  
- `if-ge [hint] src1, src2, label_name`: Branch if `src1 >= src2`.  
- `if-z [hint] src, label_name`: Branch if `src == 0`.  
- `if-nz [hint] src, label_name`: Branch if `src != 0`.  
- `return-val src`: Return value in `src` to caller.  
- `return-void`: Return from void function.  

### 4.6 Object Instantiation & Virtual Methods
- `new-instance dest, ClassName`: Instantiates heap object of `ClassName` and stores pointer in `dest`.  
- `bind-vtable dest, vtable_symbol`: Binds VTable pointer to object instance at offset 0.  
- `call-virt dest, obj_ptr, slot_index`: Indirect virtual method call via VTable slot index.  
- `call-virt-fast dest, obj_ptr, slot_index`: Monomorphic inline-cached virtual method call.  

### 4.7 Hardware Atomics & Fences
- `atomic-cas/64 [base + offset], desired`: Lock-free compare-and-swap (`lock cmpxchg`).  
- `atomic-xchg/64 [base + offset], src`: Atomic exchange (`xchg`).  
- `atomic-add/64 [base + offset], src`: Lock-free atomic add (`lock add`).  
- `fence`: Full hardware memory barrier (`mfence`).  

---

## 5. Verified Canonical Extended Smali Examples

### Example 1: Arithmetic & Function Signature
```smali
.fn calculate_sum(p0: i64, p1: i64) -> i64
    .registers 2 local
    add-int/64 v0, p0, p1
    sub-int/64 v1, v0, 500
    return-val v1
.end_fn
```

### Example 2: High-Performance Loop & Branch Hints
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

### Example 3: Object Instantiation & Field Mutation
```smali
.class Widget
    .vtable
        Widget_render
    .end_vtable
.end_class

.fn create_and_init_widget(p0: i64) -> i64
    .registers 2 local
    new-instance v0, Widget
    store-mem [v0 + 8], 777
    load-mem v1, [v0 + 8]
    add-int/64 v1, v1, p0
    return-val v1
.end_fn
```

### Example 4: Hardware Lock-Free Atomic Counter
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

## 6. LLM Validation & Self-Correction Checklist

Before outputting Extended Smali code, perform this self-audit:

1. [ ] Does every function have `.registers N local` declared immediately after `.fn`?
2. [ ] Are parameter registers (`p0`, `p1`, ...) used in order of function parameter declarations?
3. [ ] Are all instructions using 3-operand destination-first format (`opcode dest, src1, src2`)?
4. [ ] Are all basic block labels correctly formatted as `name:` and referenced accurately by `goto` / `if-*` instructions?
5. [ ] Is the function closed with `.end_fn`?
6. [ ] Are all opcodes strictly present in the Opcode Dictionary in Section 4? (Zero hallucinated instructions allowed).
