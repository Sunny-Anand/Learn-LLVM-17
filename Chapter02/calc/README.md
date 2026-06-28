# Chapter 02 — `calc`: The Expression Compiler

A small compiler that takes a simple arithmetic expression, optionally with
named variables, and emits LLVM IR.  The IR can then be compiled to a native
binary using `llc` and the provided C runtime helper `rtcalc.c`.

---

## Fixes Applied to the Book's Source

The original `CMakeLists.txt` and `src/CodeGen.cpp` needed three changes to
build against a modern LLVM (17+) on macOS/Linux:

| # | File | Problem | Fix |
|---|------|---------|-----|
| 1 | `CMakeLists.txt` | `include(ChooseMSVCCRT)` — Windows-only CMake module, does not exist on macOS/Linux | Removed the line |
| 2 | `CMakeLists.txt` | No C++ standard specified; LLVM headers require C++17 (`std::size`, `std::is_integral_v`, `std::optional`, …) | Added `set(CMAKE_CXX_STANDARD 17)` |
| 3 | `src/CodeGen.cpp` | `llvm::Module` used but `llvm/IR/Module.h` not `#include`d (used to be pulled in transitively in older LLVM) | Added `#include "llvm/IR/Module.h"` |

---

## Prerequisites

| Tool | Notes |
|------|-------|
| CMake ≥ 3.20 | |
| Ninja | `brew install ninja` |
| clang / clang++ | Apple Clang (Xcode Command Line Tools) or LLVM clang |
| Built LLVM | Must be built from source; see `llvm-project/` |

---

## Build

```bash
# From this directory (Chapter02/calc)

cmake -GNinja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DLLVM_DIR=/Users/sunnyanand79office/LLVM-FORK-MYWORK/llvm-project/build/lib/cmake/llvm \
  -B build

ninja -C build
```

The compiler binary is written to `build/src/calc`.

---

## Expression Syntax

```
<input>  ::= [ "with" <varlist> ":" ] <expr>
<varlist> ::= <ident> { "," <ident> }
<expr>    ::= <term>  { ("+" | "-") <term> }
<term>    ::= <factor> { ("*" | "/") <factor> }
<factor>  ::= <number> | <ident> | "(" <expr> ")"
```

- Variables must be declared with `with` before they can be used in the expression.
- There is **no** `=` assignment in the language; variables receive their values
  at runtime via `calc_read()`.

---

## Compiler Tests (IR output)

All commands below print LLVM IR to stdout.

```bash
# 1. Simple constant
./build/src/calc "42"

# 2. Constant folding
./build/src/calc "1 + 2"

# 3. Two variables, addition
./build/src/calc "with a, b : a + b"

# 4. Variable with multiply and add
./build/src/calc "with x : x * 2 + 3"

# 5. Parenthesised expression
./build/src/calc "with a : (a + 1) * 2"

# 6. Syntax error — variable used without declaration
./build/src/calc "a + 1"
# Expected: "Variable a not declared\nSemantic errors occured"
```

---

## End-to-End: Compile and Run a Native Binary

`llc` lives inside the LLVM build tree.  Set a convenience variable first:

```bash
export LLC=/Users/sunnyanand79office/LLVM-FORK-MYWORK/llvm-project/build/bin/llc
```

### Example — `with a: a*3`

**Step 1 — Generate LLVM IR (optional, just to inspect)**
```bash
./build/src/calc "with a: a*3"
```

Expected IR output:
```llvm
; ModuleID = 'calc.expr'
source_filename = "calc.expr"

@a.str = private constant [2 x i8] c"a\00"

define i32 @main(i32 %0, ptr %1) {
entry:
  %2 = call i32 @calc_read(ptr @a.str)
  %3 = mul nsw i32 %2, 3
  call void @calc_write(i32 %3)
  ret i32 0
}

declare i32 @calc_read(ptr)
declare void @calc_write(i32)
```

**Step 2 — Compile IR to an object file**
```bash
./build/src/calc "with a: a*3" | $LLC -filetype=obj -relocation-model=pic -o expr.o
```

**Step 3 — Link with the C runtime helper**
```bash
clang -o expr expr.o rtcalc.c
```

**Step 4 — Run**
```bash
./expr
```

Sample session:
```
Enter a value for a: 7
The result is: 21
```

### Example — `with a, b : a + b`

```bash
./build/src/calc "with a, b : a + b" | $LLC -filetype=obj -relocation-model=pic -o expr.o
clang -o expr expr.o rtcalc.c
./expr
```

Sample session:
```
Enter a value for a: 4
Enter a value for b: 6
The result is: 10
```

---

## Runtime Helper (`rtcalc.c`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `calc_read` | `int calc_read(char *name)` | Prompts the user to enter an integer for the named variable |
| `calc_write` | `void calc_write(int v)` | Prints `The result is: <v>` |
