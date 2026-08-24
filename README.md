<h1 align="center">The Pseudo Programming Language</h1>

<p align="center">
  <img src="assets/tppl-cover.png" width="760" alt="The Pseudo Programming Language">
</p>

<p align="center">
  <em>“a language for competitive programming that knows its purpose.”</em>
</p>

<p align="center">
  <a href="https://github.com/koljaPl/pseudo-programming-language/actions/workflows/ci.yml"><img src="https://github.com/koljaPl/pseudo-programming-language/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&amp;logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/status-alpha-6e7781" alt="Alpha status">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-2f81f7" alt="MIT License"></a>
</p>

<p align="center">
  <a href="docs/goals_and_versions/version_0.01/goals.md">Language</a>
  · <a href="docs/grammar.ebnf">Grammar</a>
  · <a href="#quick-start">Quick start</a>
  · <a href="#tests">Tests</a>
</p>

TPPL is a programming language built specifically for competitive programming. Its compact syntax feeds a real compiler frontend and semantic pipeline, then lowers to C++20 so contest code stays focused on the problem rather than boilerplate.

**Current alpha.** The C++ backend supports top-level functions, initialized locals, typed expressions and assignments, `if`/`else`, `while`, range and for-each loops, strings, vectors, indexing, and runtime I/O. Globals, nested-function code generation, and uninitialized locals are not emitted yet.

## A contest-shaped language

```tpp
int square(int x) {
    return x * x;
}

int main() {
    vector<int> answers = vector<int>(5, 0);

    for i in 0..5 {
        answers[i] = square(i + 1);
    }

    for answer in answers {
        print(answer);
    }

    return 0;
}
```

## Why TPPL?

- **Contest-first.** Every design choice starts from the constraints of competitive programming.
- **C++20-backed.** Keep the familiar contest toolchain while writing less ceremony.
- **Compiler-built.** Syntax, names, types, and control flow are checked before code generation.

## Compiler pipeline

```text
.tpp → Lexer → Parser → Declaration Collection → Name Resolution
     → Type Checking → Control-Flow Checking → Lowering → C++20 → g++ → executable
```

## Quick start

Build from source with CMake and a C++20 toolchain:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

./build/pseudo --emit-cpp solution.tpp > solution.cpp
g++ -std=c++20 -Iruntime/include solution.cpp -o solution
./solution
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

<p align="center">
  <code>Debug</code> · <code>Release</code> · <code>ASan + UBSan</code> · <code>strict warnings</code> · <code>generated C++</code> · <code>g++ E2E</code>
</p>

The suite covers the frontend, semantic passes, lowering, code generation, runtime behavior, recovery, determinism, and toolchain integration.

## Community

TPPL is fully open source. Built by competitors, for competitors—contributions, experiments, and ideas are welcome.

## Author

The Pseudo Programming Language was created by **Nicklas Plugin** before his 16th birthday.

He considers this the youngest age at which a programming language of this scale has been created.

<p align="center">
  <sub>Released under the <a href="LICENSE">MIT License</a>.</sub>
</p>
