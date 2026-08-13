# 🚀 Learn Anastasia Assembly: The Ultimate Beginner's Guide & Visual Handbook

Welcome to **Anastasia Assembly**! Whether you have never written a single line of assembly language or are looking to master low-level computer science, this visual handbook is built for you.

Assembly language is often thought of as mysterious, but Anastasia makes it friendly, structured, and powerful. By the end of this guide, you will understand how computer processors work, how data flows through registers and memory, and how to write high-performance algorithms directly in Anastasia Assembly!

---

## 📌 Table of Contents
1. [What is Assembly Language?](#1-what-is-assembly-language)
2. [Infographic 1: Computer Memory Hierarchy](#infographic-1-computer-memory-hierarchy)
3. [The Anastasia Mental Model: Registers & Parameters](#3-the-anastasia-mental-model-registers--parameters)
4. [Infographic 2: Anastasia Execution Lifecycle](#infographic-2-anastasia-execution-lifecycle)
5. [Structure of an Anastasia Program](#5-structure-of-an-anastasia-program)
6. [Basic Operations: Math & Variables](#6-basic-operations-math--variables)
7. [Deep Dive: 64-bit vs 32-bit Integer Wrapping](#7-deep-dive-64-bit-vs-32-bit-integer-wrapping)
8. [Control Flow: If Statements, Loops & Branching](#8-control-flow-if-statements-loops--branching)
9. [Infographic 3: Control Flow Branching Diagram](#infographic-3-control-flow-branching-diagram)
10. [Bitwise Operations & Hardware Manipulation](#10-bitwise-operations--hardware-manipulation)
11. [Heap Allocation & Object-Oriented Assembly](#11-heap-allocation--object-oriented-assembly)
12. [Infographic 4: Object Memory Layout & VTable Dispatch](#infographic-4-object-memory-layout--vtable-dispatch)
13. [Hands-On Beginner & Intermediate Projects](#13-hands-on-beginner--intermediate-projects)
    - [Project 1: Absolute Value](#project-1-absolute-value)
    - [Project 2: Odd or Even (Bitwise Test)](#project-2-odd-or-even-bitwise-test)
    - [Project 3: Factorial (1 to N Product)](#project-3-factorial-1-to-n-product)
    - [Project 4: Fibonacci Sequence](#project-4-fibonacci-sequence)
    - [Project 5: Is Prime Number?](#project-5-is-prime-number)
14. [Cheatsheet & Quick Reference](#14-cheatsheet--quick-reference)

---

## 1. What is Assembly Language?

When you write code in languages like Python or JavaScript, the computer hides everything happening inside the CPU. 

In high-level languages, you write:
```python
x = 5 + 3
```

A computer processor (CPU) doesn't understand Python or high-level variables. It only understands small, ultra-fast storage slots called **Registers** and tiny commands called **Opcodes** (Instructions).

Assembly language is the human-readable version of machine code. In Anastasia Assembly, that same Python line looks like:
```smali
move-const v0, 5
move-const v1, 3
add-int/64 v2, v0, v1
```

---

## Infographic 1: Computer Memory Hierarchy

To write great assembly code, you need to know where your data lives. Here is how memory speed and storage compare inside your machine:

```
+-----------------------------------------------------------------------+
|  REGISTERS (v0..v31, p0..p7)                                         |  <-- FASTEST (~0.5 ns)
|  Inside the CPU core. Instant access for math and logic operations.   |      Storage: Bytes
+-----------------------------------------------------------------------+
                                  |
                                  v
+-----------------------------------------------------------------------+
|  L1 / L2 / L3 CPU CACHES                                             |  <-- VERY FAST (1 - 10 ns)
|  Hardware memory caching recently used data and instructions.         |      Storage: Megabytes
+-----------------------------------------------------------------------+
                                  |
                                  v
+-----------------------------------------------------------------------+
|  MAIN SYSTEM RAM (Heap & Stack)                                      |  <-- SLOWER (50 - 100 ns)
|  Objects created with `new-instance` and memory loaded with `load-mem`|      Storage: Gigabytes
+-----------------------------------------------------------------------+
```

---

## 3. The Anastasia Mental Model: Registers & Parameters

Think of Anastasia as a chef's kitchen:
1. **Parameter Registers (`p0`, `p1`, `p2`, ...)**: Ingredients handed to the chef when the recipe begins.
2. **Virtual Registers (`v0`, `v1`, `v2`, ...)**: Scratchpads on the counter where you do active calculations.

```
                  +-----------------------------------+
                  |  FUNCTION PARAMETERS (Inputs)     |
                  |   [p0]  [p1]  [p2]  [p3]  ...     |
                  +-----------------------------------+
                                    |
                                    v  (Read / Compute)
                  +-----------------------------------+
                  |  LOCAL WORKING REGISTERS          |
                  |   [v0]  [v1]  [v2]  [v3]  ...     |
                  +-----------------------------------+
                                    |
                                    v  (Output)
                  +-----------------------------------+
                  |  RETURN VALUE (`return-val v0`)   |
                  +-----------------------------------+
```

---

## Infographic 2: Anastasia Execution Lifecycle

How does Anastasia turn your assembly code into physical hardware execution? Here is the compilation pipeline:

```
+--------------------------+
|  Anastasia Assembly Code |  (.fn, opcodes, registers)
+--------------------------+
             |
             v
+--------------------------+
|  Perfect-Hash Lexer &    |  Converts text into AST tokens
|  Smali-IR AST Parser     |  and instruction blocks
+--------------------------+
             |
             v
+--------------------------+
|  Anastasia JIT Engine &  |  Performs register allocation,
|  Target Code Lowerer     |  label resolution & optimization
+--------------------------+
             |
   +---------+---------+--------------------+
   |                   |                    |
   v                   v                    v
+-----------------+ +-----------------+ +-----------------+
|  x86_64 Native  | |  AArch64 Native | |  ARMv7 Native   |
| Machine Code    | | Machine Code    | | Machine Code    |
+-----------------+ +-----------------+ +-----------------+
   |                   |                    |
   +---------+---------+--------------------+
             |
             v
+--------------------------+
| Bare-Metal CPU Execution |  Direct CPU execution with
| & Hardware W^X Safety    |  zero interpreter overhead!
+--------------------------+
```

---

## 5. Structure of an Anastasia Program

Every Anastasia function follows a clean, predictable structure:

```smali
.fn add_two_numbers(p0: i64, p1: i64) -> i64
.registers 2 local

    add-int/64 v0, p0, p1
    return-val v0

.end_fn
```

### Line-by-Line Breakdown:
- `.fn add_two_numbers(p0: i64, p1: i64) -> i64`: Declares a function named `add_two_numbers` accepting two 64-bit integer inputs (`p0` and `p1`) and returning a 64-bit integer (`-> i64`).
- `.registers 2 local`: Allocates 2 local workspace registers (`v0` and `v1`).
- `add-int/64 v0, p0, p1`: Adds `p0` + `p1` and stores the result in `v0`.
- `return-val v0`: Returns the value inside `v0`.
- `.end_fn`: Marks the end of the function.

---

## 6. Basic Operations: Math & Variables

### A. Loading Constants (`move-const`)
To store a fixed integer into a register:
```smali
move-const v0, 42    ; Stores number 42 in v0
move-const v1, 100   ; Stores number 100 in v1
```

### B. Register Copying (`move`)
To copy a value from one register to another:
```smali
move v2, v0          ; Copies value of v0 into v2
```

### C. Basic Arithmetic
The destination register is **always the first operand**:

| Operation | Syntax | High-Level Math Equivalent |
| :--- | :--- | :--- |
| **Addition** | `add-int/64 v0, v1, v2` | `v0 = v1 + v2` |
| **Subtraction** | `sub-int/64 v0, v1, v2` | `v0 = v1 - v2` |
| **Negation** | `neg-int/64 v0, v1` | `v0 = -v1` |
| **Multiplication** | `mul-int/64 v0, v1, v2` | `v0 = v1 * v2` |
| **Division** | `div-int/64 v0, v1, v2` | `v0 = v1 / v2` |

---

## 7. Deep Dive: 64-bit vs 32-bit Integer Wrapping

Anastasia supports both 64-bit (`/64`) and 32-bit (`/32`) integer operations.

### What is 32-bit Integer Overflow?
When you add large numbers in `add-int/32`, the CPU truncates the result to 32 bits and sign-extends it.

```
64-Bit Addition (No Truncation):
  2,000,000,000 + 2,000,000,000 = 4,000,000,000

32-Bit Addition (Wraps around Two's Complement):
  2,000,000,000 + 2,000,000,000 = -294,967,296
```

### Example:
```smali
move-const v0, 2000000000
move-const v1, 2000000000
add-int/32 v2, v0, v1       ; v2 becomes -294967296 (wrapped at 32-bit boundary)
```

---

## 8. Control Flow: If Statements, Loops & Branching

In high-level languages, you write `if/else` and `while` loops. In Assembly, you use **Labels** and **Branch Instructions**.

### Comparison & Jump Rules
- `if-eq v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 == v1`
- `if-ne v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 != v1`
- `if-lt v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 < v1`
- `if-ge v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 >= v1`
- `goto :target` $\rightarrow$ Unconditional jump straight to `:target`

---

## Infographic 3: Control Flow Branching Diagram

Here is how a conditional `if (v0 >= v1)` decision branch executes step-by-step:

```
                  +-----------------------------------+
                  |      Compare Registers v0 & v1    |
                  |     (if-ge v0, v1, :is_greater)   |
                  +-----------------------------------+
                                    |
                  +-----------------+-----------------+
                  |                                   |
         Condition TRUE (v0 >= v1)           Condition FALSE (v0 < v1)
                  |                                   |
                  v                                   v
        +-------------------+               +-------------------+
        |  Jump to Label    |               | Continue straight |
        |   `:is_greater`   |               | to next line      |
        +-------------------+               +-------------------+
                  |                                   |
                  v                                   v
        +-------------------+               +-------------------+
        | Execute True Path |               | Execute False Path|
        +-------------------+               +-------------------+
```

---

## 10. Bitwise Operations & Hardware Manipulation

Bitwise operations manipulate numbers at the raw binary level (bits `0` and `1`).

```
  Bitwise AND (and-int/64):       Bitwise OR (or-int/64):        Bitwise XOR (xor-int/64):
     1 1 0 0 (12)                    1 1 0 0 (12)                   1 1 0 0 (12)
   & 1 0 1 0 (10)                  | 1 0 1 0 (10)                 ^ 1 0 1 0 (10)
   ---------                       ---------                      ---------
     1 0 0 0 (8)                     1 1 1 0 (14)                   0 1 1 0 (6)
```

### Essential Bitwise Opcodes:
- `and-int/64 v0, v1, v2` $\rightarrow$ Bitwise AND
- `or-int/64 v0, v1, v2` $\rightarrow$ Bitwise OR
- `xor-int/64 v0, v1, v2` $\rightarrow$ Bitwise XOR
- `shl-int/64 v0, v1, v2` $\rightarrow$ Shift Left (`v0 = v1 << v2`)
- `shr-int/64 v0, v1, v2` $\rightarrow$ Shift Right (`v0 = v1 >> v2`)

---

## 11. Heap Allocation & Object-Oriented Assembly

Anastasia supports native object instantiation and dynamic memory allocation directly in assembly!

### Key Opcodes:
- `new-instance v0, :ClassName` $\rightarrow$ Allocates heap memory for an object instance.
- `store-mem v0, [base + offset]` $\rightarrow$ Writes value `v0` into memory at `base + offset`.
- `load-mem v0, [base + offset]` $\rightarrow$ Reads value from `base + offset` into `v0`.
- `call-virt v0, [obj], slot` $\rightarrow$ Performs polymorphic virtual method dispatch via object VTable.

---

## Infographic 4: Object Memory Layout & VTable Dispatch

When you allocate an object with `new-instance v0, :Point`, Anastasia constructs this memory structure in the Heap:

```
HEAP MEMORY ADDRESS (v0)
+-----------------------------------------------------------------------+
|  Offset  0 ..  7  |  VTABLE POINTER  ----> Points to Virtual Method   |
|                   |                        Function Table             |
+-----------------------------------------------------------------------+
|  Offset  8 .. 11  |  CLASS ID        ----> Class Type Identifier      |
+-----------------------------------------------------------------------+
|  Offset 12 .. 15  |  GC FLAGS        ----> Garbage Collector Header   |
+-----------------------------------------------------------------------+
|  Offset 16 .. 23  |  FIELD 0 (e.g. x)----> 64-bit Payload           |
+-----------------------------------------------------------------------+
|  Offset 24 .. 31  |  FIELD 1 (e.g. y)----> 64-bit Payload           |
+-----------------------------------------------------------------------+
```

---

## 13. Hands-On Beginner & Intermediate Projects

### Project 1: Absolute Value
```smali
.fn absolute_value(p0: i64) -> i64
.registers 2 local

    move-const v0, 0
    if-ge p0, v0, :is_positive

    neg-int/64 v1, p0            ; v1 = -p0
    return-val v1

:is_positive
    return-val p0

.end_fn
```

---

### Project 2: Odd or Even (Bitwise Test)
```smali
.fn is_even(p0: i64) -> i64
.registers 2 local

    move-const v0, 1
    and-int/64 v1, p0, v0       ; v1 = p0 & 1
    if-eq v1, v0, :is_odd

    move-const v0, 1            ; 1 means True (Even)
    return-val v0

:is_odd
    move-const v0, 0            ; 0 means False (Odd)
    return-val v0

.end_fn
```

---

### Project 3: Factorial (1 to N Product)
```smali
.fn factorial(p0: i64) -> i64
.registers 3 local

    move-const v0, 1            ; Result = 1
    move-const v1, 1            ; i = 1

:loop_start
    if-ge v1, p0, :loop_body
    if-eq v1, p0, :loop_body
    return-val v0

:loop_body
    mul-int/64 v0, v0, v1
    move-const v2, 1
    add-int/64 v1, v1, v2
    goto :loop_start

.end_fn
```

---

### Project 4: Fibonacci Sequence
```smali
.fn fibonacci(p0: i64) -> i64
.registers 5 local

    move-const v0, 0            ; a = 0
    move-const v1, 1            ; b = 1
    move-const v2, 0            ; i = 0

:loop
    if-ge v2, p0, :continue_loop
    return-val v0

:continue_loop
    add-int/64 v3, v0, v1       ; temp = a + b
    move v0, v1                 ; a = b
    move v1, v3                 ; b = temp

    move-const v4, 1
    add-int/64 v2, v2, v4       ; i = i + 1
    goto :loop

.end_fn
```

---

### Project 5: Is Prime Number?
```smali
.fn is_prime(p0: i64) -> i64
.registers 5 local

    move-const v0, 2
    if-lt p0, v0, :not_prime    ; Numbers < 2 are not prime

    move-const v1, 2            ; d = 2 (divisor)

:loop
    mul-int/64 v2, v1, v1       ; v2 = d * d
    if-ge v2, p0, :check_div
    if-eq v2, p0, :check_div
    move-const v0, 1            ; Prime verified!
    return-val v0

:check_div
    div-int/64 v3, p0, v1       ; v3 = p0 / d
    mul-int/64 v3, v3, v1       ; v3 = (p0 / d) * d
    sub-int/64 v3, p0, v3       ; remainder = p0 % d
    move-const v4, 0
    if-eq v3, v4, :not_prime    ; Divisible -> Not Prime!

    move-const v4, 1
    add-int/64 v1, v1, v4       ; d = d + 1
    goto :loop

:not_prime
    move-const v0, 0
    return-val v0

.end_fn
```

---

## 14. Cheatsheet & Quick Reference

| Category | Opcode | Syntax Example | Description |
| :--- | :--- | :--- | :--- |
| **Constants** | `move-const` | `move-const v0, 10` | Stores constant value into register `v0` |
| **Register Copy** | `move` | `move v1, v0` | Copies value from `v0` into `v1` |
| **Math (64-bit)** | `add-int/64` | `add-int/64 v0, v1, v2` | `v0 = v1 + v2` |
| | `sub-int/64` | `sub-int/64 v0, v1, v2` | `v0 = v1 - v2` |
| | `mul-int/64` | `mul-int/64 v0, v1, v2` | `v0 = v1 * v2` |
| | `div-int/64` | `div-int/64 v0, v1, v2` | `v0 = v1 / v2` |
| | `neg-int/64` | `neg-int/64 v0, v1` | `v0 = -v1` |
| **Math (32-bit)** | `add-int/32` | `add-int/32 v0, v1, v2` | 32-bit addition with overflow truncation |
| | `sub-int/32` | `sub-int/32 v0, v1, v2` | 32-bit subtraction with overflow truncation |
| **Bitwise** | `and-int/64` | `and-int/64 v0, v1, v2` | Bitwise AND (`v0 = v1 & v2`) |
| | `or-int/64` | `or-int/64 v0, v1, v2` | Bitwise OR (`v0 = v1 \| v2`) |
| | `xor-int/64` | `xor-int/64 v0, v1, v2` | Bitwise XOR (`v0 = v1 ^ v2`) |
| | `shl-int/64` | `shl-int/64 v0, v1, v2` | Shift left (`v0 = v1 << v2`) |
| | `shr-int/64` | `shr-int/64 v0, v1, v2` | Shift right (`v0 = v1 >> v2`) |
| **Control Flow** | `if-eq` | `if-eq v0, v1, :target` | Branch to `:target` if `v0 == v1` |
| | `if-ne` | `if-ne v0, v1, :target` | Branch to `:target` if `v0 != v1` |
| | `if-lt` | `if-lt v0, v1, :target` | Branch to `:target` if `v0 < v1` |
| | `if-ge` | `if-ge v0, v1, :target` | Branch to `:target` if `v0 >= v1` |
| | `goto` | `goto :target` | Unconditional branch to `:target` |
| **Heap & OOP** | `new-instance` | `new-instance v0, :Class` | Heap allocate instance of `:Class` |
| | `load-mem` | `load-mem v0, [v1 + 16]` | Load 64-bit value from `v1 + 16` into `v0` |
| | `store-mem` | `store-mem v0, [v1 + 16]` | Store value in `v0` into memory at `v1 + 16` |
| | `call-virt` | `call-virt v0, [obj], 0` | Call virtual method slot 0 on object `obj` |
| **Function End** | `return-val` | `return-val v0` | Return value in `v0` to caller |
| | `return-void` | `return-void` | Return void |

---

### 🎉 Congratulations!
You are now equipped with deep knowledge of **Anastasia Assembly**, CPU execution pipelines, memory layouts, control flow branching, and bare-metal JIT concepts!
