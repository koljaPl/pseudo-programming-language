# Stages

I will be updating this file as the project progresses; for now, this version
is simply for the initial development phase:

In this file, I will outline what I will be doing and in what order:

Source code
    -
Project Initialization
    -
Lexer
    -
Tokens
    -
Parser
    -
Syntax AST
    -
Declaration collection
    -
Name resolution
    -
Type checking
    -
Control-flow validation
    -
Lowering
    -
C++20 code generation
    -
Runtime library
    -
Compilation with g++
    -
Executable Code

And here are the stages of the project:

1. Specification and EBNF
2. CMake, CLI, SourceManager, diagnostics
3. Tokeniser and lexer
4. Expression AST and expression parser
5. Statements, blocks and function parser
6. AST printer
7. Minimal ‘main’ + ‘print’ + first C++ code generation
8. TypeContext, symbols and scopes
9. Declaration collection
10. Name resolution
11. Type checking
12. Control-flow checking
13. General top-level functions
14. Strings and chars
15. `vector<T>` and indexing
16. Runtime input/output
17. for-range and for-each
18. Lowering
19. Reliable g++ execution
20. Error recovery and a complete test suite

Language:               The Pseudo Programming Language
File extension:         .tpp
Compiler command:       pseudo
C++ namespace:          tpp
CMake library:          tpp_compiler
CMake alias:            tpp::compiler
