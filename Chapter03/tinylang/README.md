# Chapter 03 — `tinylang`: The Front-End Compiler

A full front-end (lexer + parser + semantic analysis) for a subset of the
Modula-2 language called **tinylang**.  The compiler reads a `.mod` source
file, tokenises it, builds an AST, and performs semantic validation.
No code generation yet — that arrives in later chapters.

---

## LLVM Version Requirement

> **LLVM 20 source headers are required.**

The tinylang headers include `llvm/Support/Casting.h`, `llvm/Support/Compiler.h`,
and other LLVM headers that use **C++17** features (`std::is_same_v`,
`std::is_pointer_v`, `std::is_base_of_v`, `std::conditional_t`, …).

These are only available when:

1. The compiler used is **Clang 20 from Homebrew** (or any clang with
   full C++17 support) — **not** Apple's `/usr/bin/clang++`.
2. The LLVM **source tree** headers are on the include path (not just the
   build-generated headers).

Apple's system clang (`/usr/bin/clang++`) defaults to an older language
mode and will produce errors like:

```
error: no template named 'is_same_v' in namespace 'std'
error: no template named 'is_base_of_v' in namespace 'std'
fatal error: 'llvm/Support/Casting.h' file not found
```

---

## Fixes Applied to the Book's Source

The book's original source needed two changes to build against LLVM 20
on macOS with a source-built LLVM tree:

| # | File | Problem | Fix |
|---|------|---------|-----|
| 1 | `CMakeLists.txt` | `include_directories` used `${LLVM_INCLUDE_DIR}` **(singular)** — only points to `build/include` (generated headers), missing the source-tree headers like `Casting.h`, `Compiler.h` | Changed to `${LLVM_INCLUDE_DIRS}` **(plural)** — includes both `llvm/include` (source) and `build/include` (generated) |
| 2 | `lib/Sema/Sema.cpp:373` | `Literal.endswith("H")` — `endswith` was renamed to `ends_with` in LLVM 16+ | Changed to `Literal.ends_with("H")` |

### Why Chapter 02 worked with the same `clang`

Chapter 02's `CMakeLists.txt` already used `${LLVM_INCLUDE_DIRS}` (plural) at
line 16, so it found both header trees.  Chapter 03's original used the
singular form — a subtle book typo that only matters when pointing at a
source-built LLVM (where source and build headers live in separate directories).

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.20 | |
| Ninja | any | `brew install ninja` |
| Clang | 20 (Homebrew) | **Must use full path** — see below |
| LLVM | built from source | `llvm-project/build/` |

### Why the full Homebrew clang path is required

```
/opt/homebrew/Cellar/llvm/20.1.7/bin/clang
/opt/homebrew/Cellar/llvm/20.1.7/bin/clang++
```

When CMake is invoked with just `clang` / `clang++`, macOS resolves them to
`/usr/bin/clang++` (Apple's system Clang).  Apple's clang does **not**
automatically enable C++17 mode for LLVM's `HandleLLVMOptions`-driven
builds, causing the `std::is_same_v` / `std::is_base_of_v` errors above.

**Permanent fix** — add to `~/.zshrc`:

```zsh
export PATH="/opt/homebrew/Cellar/llvm/20.1.7/bin:$PATH"
```

After `source ~/.zshrc`, the short names `clang` / `clang++` resolve to
the Homebrew version and the full paths are no longer needed.

---

## Build

Run from **any directory** — absolute paths are used throughout to avoid
shell `$PWD` confusion.

```bash
rm -rf /Users/sunnyanand79office/LLVM-FORK-MYWORK/Learn-LLVM-17/Chapter03/tinylang/build && \
cmake -GNinja \
  -DCMAKE_C_COMPILER=/opt/homebrew/Cellar/llvm/20.1.7/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/Cellar/llvm/20.1.7/bin/clang++ \
  -DLLVM_DIR=/Users/sunnyanand79office/LLVM-FORK-MYWORK/llvm-project/build/lib/cmake/llvm \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -S /Users/sunnyanand79office/LLVM-FORK-MYWORK/Learn-LLVM-17/Chapter03/tinylang \
  -B /Users/sunnyanand79office/LLVM-FORK-MYWORK/Learn-LLVM-17/Chapter03/tinylang/build && \
ninja -C /Users/sunnyanand79office/LLVM-FORK-MYWORK/Learn-LLVM-17/Chapter03/tinylang/build
```

> `-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0` silences the `ld: warning: object file
> was built for newer macOS version` linker warnings that appear when the
> source-built LLVM targets macOS 26.0 but the linker defaults to 16.0.

The compiler binary is written to:

```
build/tools/driver/tinylang
```

---

## Running the Example

### `Gcd.mod` — GCD algorithm in tinylang

```bash
/Users/sunnyanand79office/LLVM-FORK-MYWORK/Learn-LLVM-17/Chapter03/tinylang/build/tools/driver/tinylang \
  /Users/sunnyanand79office/LLVM-FORK-MYWORK/Learn-LLVM-17/Chapter03/tinylang/example/Gcd.mod
```

Expected output:

```
Tinylang 0.1
```

No errors means the file passed all three front-end stages:
lexical analysis → parsing → semantic analysis.

### What `Gcd.mod` contains

```modula2
MODULE Gcd;

VAR x: INTEGER;

PROCEDURE GCD(a, b: INTEGER) : INTEGER;
VAR t: INTEGER;
BEGIN
  IF b = 0 THEN
    RETURN a;
  END;
  WHILE b # 0 DO
    t := a MOD b;
    a := b;
    b := t;
  END;
  RETURN a;
END GCD;

END Gcd.
```

---

## Front-End Pipeline

```
Gcd.mod (source text)
       │
       ▼
   Lexer  (lib/Lexer/Lexer.cpp)
       │  Token stream
       ▼
   Parser (lib/Parser/Parser.cpp)
       │  AST
       ▼
   Sema   (lib/Sema/Sema.cpp)
       │  Validated AST
       ▼
  [Code generation — Chapter 04+]
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `fatal error: 'llvm/Support/Casting.h' file not found` | `LLVM_INCLUDE_DIR` (singular) missing source headers | Fixed in `CMakeLists.txt` — use `LLVM_INCLUDE_DIRS` (plural) |
| `error: no template named 'is_same_v'` | Apple's `/usr/bin/clang++` used instead of Homebrew clang | Use full path `/opt/homebrew/Cellar/llvm/20.1.7/bin/clang++` |
| `error: no member named 'endswith'` | API renamed in LLVM 16+ | Fixed in `lib/Sema/Sema.cpp` — use `ends_with` |
| `CMake Error: source directory does not exist` | Running cmake from wrong directory | Use absolute `-S` path as shown in Build section above |
| `ld: warning: built for newer macOS version` | LLVM libs target macOS 26.0, linker defaults to 16.0 | Add `-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0` to cmake |
