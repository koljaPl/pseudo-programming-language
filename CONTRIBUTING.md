# Contributing to TPPL

Thank you for helping improve The Pseudo Programming Language. TPPL is an
alpha-stage compiler built for competitive programming, and focused,
well-tested contributions are welcome.

By participating, you agree to follow the project
[Code of Conduct](CODE_OF_CONDUCT.md).

## Ways to contribute

Useful contributions include:

- reproducible bug reports;
- compiler, runtime, and toolchain fixes;
- tests for existing language behavior;
- clearer diagnostics and recovery;
- documentation improvements;
- carefully scoped language proposals.

Please report security-sensitive issues privately as described in
[SECURITY.md](SECURITY.md), not in a public issue.

## Before you start

Search the [existing issues](https://github.com/koljaPl/pseudo-programming-language/issues)
before opening a new one. Small fixes and documentation improvements may go
directly to a pull request. Please open an issue before changing any of the
following contracts:

- grammar or language semantics;
- AST or semantic-pass interfaces;
- Lowered IR;
- runtime behavior;
- CLI behavior or exit codes.

This keeps larger changes aligned with the current roadmap and avoids parallel
implementations of the same idea. TPPL intentionally does not accept every
general-purpose language feature: proposals should explain how they serve
competitive programming.

## Development setup

You need CMake 3.20 or newer and a C++20 compiler. GCC and Clang are exercised
by CI. A local `g++` installation enables the generated-C++ compilation and
execution tests.

Configure, build, and run the test suite:

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To inspect a source file through the CLI:

```bash
./build/pseudo program.tpp
./build/pseudo --dump-ast program.tpp
./build/pseudo --emit-cpp program.tpp
```

## Engineering guidelines

- Keep the project on C++20 and follow its existing RAII and value-semantics
  style.
- Avoid global mutable state, singleton ownership, unnecessary dependencies,
  and abstraction layers without a concrete use.
- Preserve the compiler boundary:
  semantic passes validate the AST, Lowering owns normalization, and code
  generation consumes Lowered IR rather than repeating name or type analysis.
- Preserve deterministic output and source-order diagnostics.
- Treat malformed input and inconsistent manually constructed states safely:
  no crashes, hangs, undefined behavior, or partial requested output.
- Keep changes focused. Do not mix unrelated cleanup with a behavioral change.

There is currently no mandatory formatter. Match the surrounding style and
ensure the build remains warning-free under the flags configured by the
project.

## Tests and documentation

Every behavioral fix should include a regression test that fails without the
fix. Add tests at the narrowest useful level, then include integration or E2E
coverage when the behavior crosses compiler stages.

When a change affects language syntax or semantics, update the
[grammar](docs/grammar.ebnf) and relevant
[goals](docs/goals_and_versions/version_0.01/goals.md) together with the
applicable parser, semantic, lowering, code-generation, CLI, or execution
tests. Do not document planned behavior as already implemented.

Run at least the Debug build and full CTest suite before opening a pull
request. For toolchain, runtime, or code-generation changes, also run the
relevant generated-C++ and `g++` E2E tests available in CTest.

## Pull requests

Create a focused branch from the current `main` branch and target `main` unless
a maintainer explicitly requests a version branch.

In the pull request description:

1. explain the problem and intended behavior;
2. summarize the implementation;
3. list the commands and tests actually run;
4. identify anything that could not be verified locally.

[Conventional Commits](https://www.conventionalcommits.org/) are preferred for
commit messages, for example:

```text
fix(parser): preserve progress after malformed delimiters
test(runtime): cover signed input boundaries
docs: clarify the supported backend subset
```

This convention is encouraged for a clear history, but is not enforced by an
automated commit-message check.

### Checklist

- [ ] The change is focused and contains no unrelated generated artifacts.
- [ ] New behavior or a bug fix has appropriate tests.
- [ ] User-facing behavior and documentation agree.
- [ ] `ctest --test-dir build --output-on-failure` passes.
- [ ] `git diff --check` reports no whitespace errors.
- [ ] The pull request states what was and was not verified.
