# 🚀 Learn Anastasia Assembly: The Absolute Beginner's Guide

Welcome to **Anastasia Assembly**! If you have never written a single line of assembly language before, you are in the right place. 

Assembly language is often thought of as mysterious or intimidating, but Anastasia makes it friendly, expressive, and super fun to learn. By the end of this guide, you will understand how computer processors think, how variables work under the hood, and how to write complete algorithms directly in Anastasia Assembly!

---

## 📌 Table of Contents
1. [What is Assembly Language?](#1-what-is-assembly-language)
2. [The Anastasia Mental Model: Registers & Instructions](#2-the-anastasia-mental-model-registers--instructions)
3. [Structure of an Anastasia Program](#3-structure-of-an-anastasia-program)
4. [Basic Operations: Math & Variables](#4-basic-operations-math--variables)
5. [Control Flow: If Statements & Loops](#5-control-flow-if-statements--loops)
6. [Bitwise Operations: Talking to the Hardware](#6-bitwise-operations-talking-to-the-hardware)
7. [Hands-On Practice Projects](#7-hands-on-practice-projects)
   - [Project 1: Absolute Value](#project-1-absolute-value)
   - [Project 2: Factorial (1 to N Product)](#project-2-factorial-1-to-n-product)
   - [Project 3: Fibonacci Sequence](#project-3-fibonacci-sequence)
8. [Cheatsheet & Quick Reference](#8-cheatsheet--quick-reference)

---

## 1. What is Assembly Language?

When you write code in languages like Python or JavaScript, the computer hides everything happening inside the CPU. 

In high-level languages, you write:
```python
x = 5 + 3
```

A computer processor (CPU) doesn't understand Python or high-level variables. It only understands small boxes of memory called **Registers** and tiny commands called **Opcodes** (Instructions).

Assembly language is the human-readable version of raw machine code. In Anastasia Assembly, that same Python line looks like:
```smali
move-const v0, 5
move-const v1, 3
add-int/64 v2, v0, v1
```

---

## 2. The Anastasia Mental Model: Registers & Instructions

Think of Anastasia as a workbench with:
1. **Parameter Boxes (`p0`, `p1`, `p2`, ...)**: Inputs passed into your function.
2. **Virtual Registers (`v0`, `v1`, `v2`, ...)**: Working scratchpads where you store temporary numbers and calculate results.

### High-Level vs Anastasia Assembly Analogy:

| High-Level Concept | Anastasia Assembly Equivalent | Real-Life Analogy |
| :--- | :--- | :--- |
| Function Input | `p0`, `p1` | Ingredients handed to a chef |
| Local Variable | `v0`, `v1`, `v2` | Scratch paper on the chef's counter |
| Command | `add-int/64`, `sub-int/64` | Steps in a recipe |
| Return Value | `return-val v0` | Serving the final dish |

---

## 3. Structure of an Anastasia Program

Every Anastasia function follows a clean structure:

```smali
.fn add_two_numbers(p0: i64, p1: i64) -> i64
.registers 2 local

    add-int/64 v0, p0, p1
    return-val v0

.end_fn
```

### Let's break down every line:
- `.fn add_two_numbers(p0: i64, p1: i64) -> i64`: Declares a function named `add_two_numbers` taking two 64-bit integer parameters (`p0` and `p1`) and returning a 64-bit integer (`-> i64`).
- `.registers 2 local`: Tells Anastasia we need 2 local workspace registers (`v0` and `v1`).
- `add-int/64 v0, p0, p1`: Adds `p0` + `p1` and puts the answer into `v0`.
- `return-val v0`: Returns the value stored in `v0` back to the caller.
- `.end_fn`: Marks the end of the function.

---

## 4. Basic Operations: Math & Variables

### A. Loading Numbers into Registers (`move-const`)
To store a fixed number (constant) into a register:
```smali
move-const v0, 42    ; Stores 42 into register v0
move-const v1, 100   ; Stores 100 into register v1
```

### B. Copying Values (`move`)
To copy a value from one register to another:
```smali
move v2, v0          ; Copies the value inside v0 into v2
```

### C. Basic Arithmetic
Anastasia provides intuitive math instructions. The destination register is **always the first parameter**:

| Operation | Syntax | What it does in High-Level Code |
| :--- | :--- | :--- |
| **Addition** | `add-int/64 v0, v1, v2` | `v0 = v1 + v2` |
| **Subtraction** | `sub-int/64 v0, v1, v2` | `v0 = v1 - v2` |
| **Negation** | `neg-int/64 v0, v1` | `v0 = -v1` |
| **Multiplication** | `mul-int/64 v0, v1, v2` | `v0 = v1 * v2` |
| **Division** | `div-int/64 v0, v1, v2` | `v0 = v1 / v2` |

---

## 5. Control Flow: If Statements & Loops

In high-level languages, you use `if` blocks and `while` loops. In Assembly, control flow is built using **Labels** and **Conditional Jumps** (also called branches).

### Labels
A label is a bookmark in your code ending with a colon `:`:
```smali
:loop_start
```

### Comparison & Jump Instructions
- `if-eq v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 == v1`
- `if-ne v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 != v1`
- `if-lt v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 < v1`
- `if-ge v0, v1, :target` $\rightarrow$ Jump to `:target` if `v0 >= v1`
- `goto :target` $\rightarrow$ Unconditionally jump straight to `:target`

---

## 6. Bitwise Operations: Talking to the Hardware

For low-level algorithms, bitwise math allows blazing fast operations:

- `and-int/64 v0, v1, v2` $\rightarrow$ Bitwise AND (`v0 = v1 & v2`)
- `or-int/64 v0, v1, v2` $\rightarrow$ Bitwise OR (`v0 = v1 | v2`)
- `xor-int/64 v0, v1, v2` $\rightarrow$ Bitwise XOR (`v0 = v1 ^ v2`)
- `shl-int/64 v0, v1, v2` $\rightarrow$ Shift Left (`v0 = v1 << v2`)
- `shr-int/64 v0, v1, v2` $\rightarrow$ Shift Right (`v0 = v1 >> v2`)

---

## 7. Hands-On Practice Projects

### Project 1: Absolute Value
*Goal*: Take a parameter `p0`. If `p0` is negative, flip its sign so it becomes positive.

```smali
.fn absolute_value(p0: i64) -> i64
.registers 2 local

    move-const v0, 0
    if-ge p0, v0, :is_positive    ; If p0 >= 0, jump to :is_positive

    neg-int/64 v1, p0            ; v1 = -p0 (flip negative to positive)
    return-val v1

:is_positive
    return-val p0                ; Return p0 directly

.end_fn
```

---

### Project 2: Factorial (1 to N Product)
*Goal*: Calculate $N! = 1 \times 2 \times 3 \times \dots \times N$ for input `p0`.

```smali
.fn factorial(p0: i64) -> i64
.registers 3 local

    move-const v0, 1    ; Accumulator (result) initialized to 1
    move-const v1, 1    ; Counter (i) initialized to 1

:loop_start
    if-ge v1, p0, :loop_body
    if-eq v1, p0, :loop_body
    return-val v0       ; When counter > p0, return result

:loop_body
    mul-int/64 v0, v0, v1    ; result = result * i
    move-const v2, 1
    add-int/64 v1, v1, v2    ; i = i + 1
    goto :loop_start

.end_fn
```

---

### Project 3: Fibonacci Sequence
*Goal*: Compute the $N$-th Fibonacci number ($0, 1, 1, 2, 3, 5, 8, 13, \dots$).

```smali
.fn fibonacci(p0: i64) -> i64
.registers 5 local

    move-const v0, 0    ; a = 0
    move-const v1, 1    ; b = 1
    move-const v2, 0    ; i = 0

:loop
    if-ge v2, p0, :continue_loop
    return-val v0       ; Return a

:continue_loop
    add-int/64 v3, v0, v1   ; temp = a + b
    move v0, v1             ; a = b
    move v1, v3             ; b = temp

    move-const v4, 1
    add-int/64 v2, v2, v4   ; i = i + 1
    goto :loop

.end_fn
```

---

## 8. Cheatsheet & Quick Reference

| Command Category | Instruction | Example | Meaning |
| :--- | :--- | :--- | :--- |
| **Constants** | `move-const` | `move-const v0, 10` | Set `v0 = 10` |
| **Copy** | `move` | `move v1, v0` | Set `v1 = v0` |
| **Arithmetic** | `add-int/64` | `add-int/64 v0, v1, v2` | `v0 = v1 + v2` |
| | `sub-int/64` | `sub-int/64 v0, v1, v2` | `v0 = v1 - v2` |
| | `mul-int/64` | `mul-int/64 v0, v1, v2` | `v0 = v1 * v2` |
| | `div-int/64` | `div-int/64 v0, v1, v2` | `v0 = v1 / v2` |
| | `neg-int/64` | `neg-int/64 v0, v1` | `v0 = -v1` |
| **Logic** | `and-int/64` | `and-int/64 v0, v1, v2` | `v0 = v1 & v2` |
| | `or-int/64` | `or-int/64 v0, v1, v2` | `v0 = v1 \| v2` |
| | `xor-int/64` | `xor-int/64 v0, v1, v2` | `v0 = v1 ^ v2` |
| **Branches** | `if-eq` | `if-eq v0, v1, :lbl` | Jump if `v0 == v1` |
| | `if-ne` | `if-ne v0, v1, :lbl` | Jump if `v0 != v1` |
| | `if-lt` | `if-lt v0, v1, :lbl` | Jump if `v0 < v1` |
| | `if-ge` | `if-ge v0, v1, :lbl` | Jump if `v0 >= v1` |
| | `goto` | `goto :lbl` | Jump to `:lbl` |
| **Returns** | `return-val` | `return-val v0` | Return value in `v0` |
| | `return-void` | `return-void` | Return nothing |

---

### 🎉 Congratulations!
You have now learned the fundamentals of **Anastasia Assembly**! You can write low-level functions, execute loops, perform bitwise math, and understand how bare-metal JIT engines execute code!
