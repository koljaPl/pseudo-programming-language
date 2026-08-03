cmake_minimum_required(VERSION 3.20)

project(
    Pseudo
    VERSION 0.1.0
    LANGUAGES CXX
)

set(TPP_PRERELEASE "alpha")

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated/pseudo")

configure_file(
    cmake/config.hpp.in
    "${PROJECT_BINARY_DIR}/generated/pseudo/config.hpp"
    @ONLY
)

include(cmake/CompilerWarnings.cmake)

add_library(tpp_compiler STATIC
    src/ast/printer.cpp
    src/source/source_manager.cpp
    src/diagnostics/diagnostic_engine.cpp
    src/lexer/lexer.cpp
    src/parser/expression_parser.cpp
    src/parser/parser.cpp
    src/driver/compiler.cpp
)

add_library(tpp::compiler ALIAS tpp_compiler)

target_compile_features(tpp_compiler PUBLIC cxx_std_20)

target_include_directories(tpp_compiler
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>"
)

set_target_properties(
    tpp_compiler
    PROPERTIES CXX_EXTENSIONS OFF
)

tpp_enable_warnings(tpp_compiler)

add_executable(pseudo
    app/main.cpp
    app/cli.cpp
)

target_include_directories(pseudo PRIVATE app)
target_link_libraries(pseudo PRIVATE tpp::compiler)
target_compile_features(pseudo PRIVATE cxx_std_20)

set_target_properties(
    pseudo
    PROPERTIES CXX_EXTENSIONS OFF
)

tpp_enable_warnings(pseudo)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
