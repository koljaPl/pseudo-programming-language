function(tpp_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE tpp::compiler)
    target_include_directories(
        ${target}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/app"
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    tpp_enable_warnings(${target})

    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_executable(
    process_test_helper
    process_test_helper.cpp
)

target_compile_features(process_test_helper PRIVATE cxx_std_20)
set_target_properties(process_test_helper PROPERTIES CXX_EXTENSIONS OFF)
tpp_enable_warnings(process_test_helper)

tpp_add_test(
    source_manager_tests
    source_manager_tests.cpp
)

tpp_add_test(
    diagnostic_tests
    diagnostic_tests.cpp
)

tpp_add_test(
    lexer_tests
    lexer_tests.cpp
)

tpp_add_test(
    expression_parser_tests
    expression_parser_tests.cpp
)

tpp_add_test(
    program_parser_tests
    program_parser_tests.cpp
)

tpp_add_test(
    ast_printer_tests
    ast_printer_tests.cpp
)

tpp_add_test(
    cpp_codegen_tests
    cpp_codegen_tests.cpp
)

tpp_add_test(
    lowerer_tests
    lowerer_tests.cpp
)

tpp_add_test(
    gpp_compiler_tests
    gpp_compiler_tests.cpp
)

find_package(Threads REQUIRED)

add_dependencies(gpp_compiler_tests process_test_helper)
target_link_libraries(gpp_compiler_tests PRIVATE Threads::Threads)

target_compile_definitions(
    gpp_compiler_tests
    PRIVATE
        TPP_PROCESS_TEST_HELPER="$<TARGET_FILE:process_test_helper>"
        TPP_RUNTIME_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/runtime/include"
)

tpp_add_test(
    runtime_tests
    runtime_tests.cpp
)

tpp_add_test(
    type_context_tests
    type_context_tests.cpp
)

tpp_add_test(
    symbol_table_tests
    symbol_table_tests.cpp
)

tpp_add_test(
    declaration_collector_tests
    declaration_collector_tests.cpp
)

tpp_add_test(
    name_resolver_tests
    name_resolver_tests.cpp
)

tpp_add_test(
    type_checker_tests
    type_checker_tests.cpp
)

tpp_add_test(
    control_flow_checker_tests
    control_flow_checker_tests.cpp
)

tpp_add_test(
    driver_tests
    driver_tests.cpp
)

target_compile_definitions(
    driver_tests
    PRIVATE TPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

tpp_add_test(
    cli_tests
    cli_tests.cpp
    "${PROJECT_SOURCE_DIR}/app/cli.cpp"
)

target_compile_definitions(
    cli_tests
    PRIVATE TPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

function(tpp_add_cli_integration_test name)
    cmake_parse_arguments(
        TEST
        ""
        "EXIT_CODE;STDOUT;STDERR;STDOUT_FILE;STDERR_FILE"
        "ARGS"
        ${ARGN}
    )

    if(NOT DEFINED TEST_EXIT_CODE)
        message(FATAL_ERROR "${name}: EXIT_CODE is required")
    endif()

    list(LENGTH TEST_ARGS argument_count)
    set(argument_definitions)

    if(argument_count GREATER 0)
        math(EXPR last_argument "${argument_count} - 1")

        foreach(index RANGE "${last_argument}")
            list(GET TEST_ARGS "${index}" argument)
            list(APPEND
                argument_definitions
                "-DCLI_ARG_${index}=${argument}"
            )
        endforeach()
    endif()

    add_test(
        NAME "cli.integration.${name}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DCLI_ARG_COUNT=${argument_count}"
            ${argument_definitions}
            "-DEXPECTED_EXIT=${TEST_EXIT_CODE}"
            "-DEXPECTED_STDOUT=${TEST_STDOUT}"
            "-DEXPECTED_STDERR=${TEST_STDERR}"
            "-DEXPECTED_STDOUT_FILE=${TEST_STDOUT_FILE}"
            "-DEXPECTED_STDERR_FILE=${TEST_STDERR_FILE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCliTest.cmake"
    )
endfunction()

string(
    CONCAT
    help_output
    "Usage: pseudo [options] <file.tpp>\n"
    "\n"
    "Options:\n"
    "  -h, --help     Show this help message\n"
    "  -v, --version  Show version information\n"
    "      --dump-ast Print the parsed AST\n"
    "      --emit-cpp Emit generated C++20 source\n"
)

tpp_add_cli_integration_test(
    no_arguments
    EXIT_CODE 2
    STDERR "pseudo: error: no input file\n"
)

tpp_add_cli_integration_test(
    help
    EXIT_CODE 0
    STDOUT "${help_output}"
    ARGS --help
)

tpp_add_cli_integration_test(
    version
    EXIT_CODE 0
    STDOUT "Pseudo ${PROJECT_VERSION}-${TPP_PRERELEASE}\n"
    ARGS --version
)

tpp_add_cli_integration_test(
    unknown_option
    EXIT_CODE 2
    STDERR "pseudo: error: unknown option '--wat'\n"
    ARGS --wat
)

set(empty_source "${CMAKE_CURRENT_SOURCE_DIR}/data/empty.tpp")

tpp_add_cli_integration_test(
    multiple_inputs
    EXIT_CODE 2
    STDERR "pseudo: error: expected exactly one input file\n"
    ARGS "${empty_source}" "${empty_source}"
)

tpp_add_cli_integration_test(
    empty_source
    EXIT_CODE 0
    ARGS "${empty_source}"
)

tpp_add_cli_integration_test(
    dump_empty_ast
    EXIT_CODE 0
    STDOUT "Program\n"
    ARGS --dump-ast "${empty_source}"
)

set(valid_lexical_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/valid_lexical.tpp"
)

tpp_add_cli_integration_test(
    valid_lexical_source
    EXIT_CODE 0
    ARGS "${valid_lexical_source}"
)

string(
    CONCAT
    valid_ast_output
    "Program\n"
    "  Function name=\"main\" return=int\n"
    "    Parameters\n"
    "    Block\n"
    "      Return\n"
    "        IntegerLiteral value=\"0\"\n"
)

tpp_add_cli_integration_test(
    dump_valid_ast
    EXIT_CODE 0
    STDOUT "${valid_ast_output}"
    ARGS "${valid_lexical_source}" --dump-ast
)

set(codegen_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_minimal.tpp"
)
set(codegen_expected
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_minimal.expected.cpp"
)

tpp_add_cli_integration_test(
    emit_cpp
    EXIT_CODE 0
    STDOUT_FILE "${codegen_expected}"
    ARGS --emit-cpp "${codegen_source}"
)

set(codegen_functions_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_functions.tpp"
)
set(codegen_functions_expected
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_functions.expected.cpp"
)

tpp_add_cli_integration_test(
    emit_cpp_top_level_functions
    EXIT_CODE 0
    STDOUT_FILE "${codegen_functions_expected}"
    ARGS --emit-cpp "${codegen_functions_source}"
)

set(codegen_strings_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_strings.tpp"
)
set(codegen_strings_expected
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_strings.expected.cpp"
)

tpp_add_cli_integration_test(
    emit_cpp_strings_and_chars
    EXIT_CODE 0
    STDOUT_FILE "${codegen_strings_expected}"
    ARGS --emit-cpp "${codegen_strings_source}"
)

set(codegen_vectors_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_vectors.tpp"
)
set(codegen_vectors_expected
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_vectors.expected.cpp"
)

tpp_add_cli_integration_test(
    emit_cpp_vectors
    EXIT_CODE 0
    STDOUT_FILE "${codegen_vectors_expected}"
    ARGS --emit-cpp "${codegen_vectors_source}"
)

set(codegen_runtime_io_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_runtime_io.tpp"
)
set(codegen_runtime_io_expected
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_runtime_io.expected.cpp"
)

tpp_add_cli_integration_test(
    emit_cpp_runtime_io
    EXIT_CODE 0
    STDOUT_FILE "${codegen_runtime_io_expected}"
    ARGS --emit-cpp "${codegen_runtime_io_source}"
)

set(codegen_loops_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_loops.tpp"
)
set(codegen_loops_expected
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_loops.expected.cpp"
)

tpp_add_cli_integration_test(
    emit_cpp_loops
    EXIT_CODE 0
    STDOUT_FILE "${codegen_loops_expected}"
    ARGS --emit-cpp "${codegen_loops_source}"
)

tpp_add_cli_integration_test(
    conflicting_output_modes
    EXIT_CODE 2
    STDERR
        "pseudo: error: options '--dump-ast' and '--emit-cpp' cannot be used together\n"
    ARGS --dump-ast --emit-cpp "${codegen_source}"
)

set(invalid_lexical_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/invalid_lexical.tpp"
)

string(
    CONCAT
    invalid_lexical_diagnostic
    "${invalid_lexical_source}:1:1: error: unknown character '@'\n"
    "  1 | @\n"
    "    | ^\n"
)

tpp_add_cli_integration_test(
    invalid_lexical_source
    EXIT_CODE 1
    STDERR "${invalid_lexical_diagnostic}"
    ARGS "${invalid_lexical_source}"
)

tpp_add_cli_integration_test(
    suppress_dump_on_lexical_error
    EXIT_CODE 1
    STDERR "${invalid_lexical_diagnostic}"
    ARGS --dump-ast "${invalid_lexical_source}"
)

tpp_add_cli_integration_test(
    suppress_emit_on_lexical_error
    EXIT_CODE 1
    STDERR "${invalid_lexical_diagnostic}"
    ARGS --emit-cpp "${invalid_lexical_source}"
)

set(invalid_syntax_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/invalid_syntax.tpp"
)

string(
    CONCAT
    invalid_syntax_diagnostic
    "${invalid_syntax_source}:1:9: error: expected expression\n"
    "  1 | int x = }\n"
    "    |         ^\n"
)

tpp_add_cli_integration_test(
    invalid_syntax_source
    EXIT_CODE 1
    STDERR "${invalid_syntax_diagnostic}"
    ARGS "${invalid_syntax_source}"
)

tpp_add_cli_integration_test(
    suppress_dump_on_syntax_error
    EXIT_CODE 1
    STDERR "${invalid_syntax_diagnostic}"
    ARGS --dump-ast "${invalid_syntax_source}"
)

tpp_add_cli_integration_test(
    suppress_emit_on_syntax_error
    EXIT_CODE 1
    STDERR "${invalid_syntax_diagnostic}"
    ARGS --emit-cpp "${invalid_syntax_source}"
)

set(duplicate_declaration_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/duplicate_declaration.tpp"
)
set(duplicate_declaration_stderr
    "${CMAKE_CURRENT_BINARY_DIR}/duplicate_declaration.expected.stderr"
)
configure_file(
    data/duplicate_declaration.expected.stderr.in
    "${duplicate_declaration_stderr}"
    @ONLY
)

tpp_add_cli_integration_test(
    duplicate_declaration
    EXIT_CODE 1
    STDERR_FILE "${duplicate_declaration_stderr}"
    ARGS "${duplicate_declaration_source}"
)

tpp_add_cli_integration_test(
    suppress_dump_on_declaration_error
    EXIT_CODE 1
    STDERR_FILE "${duplicate_declaration_stderr}"
    ARGS --dump-ast "${duplicate_declaration_source}"
)

tpp_add_cli_integration_test(
    suppress_emit_on_declaration_error
    EXIT_CODE 1
    STDERR_FILE "${duplicate_declaration_stderr}"
    ARGS --emit-cpp "${duplicate_declaration_source}"
)

set(unknown_name_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/unknown_name.tpp"
)
set(unknown_name_stderr
    "${CMAKE_CURRENT_BINARY_DIR}/unknown_name.expected.stderr"
)
configure_file(
    data/unknown_name.expected.stderr.in
    "${unknown_name_stderr}"
    @ONLY
)

tpp_add_cli_integration_test(
    unknown_name
    EXIT_CODE 1
    STDERR_FILE "${unknown_name_stderr}"
    ARGS "${unknown_name_source}"
)

tpp_add_cli_integration_test(
    suppress_dump_on_name_resolution_error
    EXIT_CODE 1
    STDERR_FILE "${unknown_name_stderr}"
    ARGS --dump-ast "${unknown_name_source}"
)

tpp_add_cli_integration_test(
    suppress_emit_on_name_resolution_error
    EXIT_CODE 1
    STDERR_FILE "${unknown_name_stderr}"
    ARGS --emit-cpp "${unknown_name_source}"
)

set(type_error_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/type_error.tpp"
)
set(type_error_stderr
    "${CMAKE_CURRENT_BINARY_DIR}/type_error.expected.stderr"
)
configure_file(
    data/type_error.expected.stderr.in
    "${type_error_stderr}"
    @ONLY
)

tpp_add_cli_integration_test(
    type_error
    EXIT_CODE 1
    STDERR_FILE "${type_error_stderr}"
    ARGS "${type_error_source}"
)

tpp_add_cli_integration_test(
    suppress_dump_on_type_error
    EXIT_CODE 1
    STDERR_FILE "${type_error_stderr}"
    ARGS --dump-ast "${type_error_source}"
)

tpp_add_cli_integration_test(
    suppress_emit_on_type_error
    EXIT_CODE 1
    STDERR_FILE "${type_error_stderr}"
    ARGS --emit-cpp "${type_error_source}"
)

set(control_flow_error_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/control_flow_error.tpp"
)
set(control_flow_error_stderr
    "${CMAKE_CURRENT_BINARY_DIR}/control_flow_error.expected.stderr"
)
configure_file(
    data/control_flow_error.expected.stderr.in
    "${control_flow_error_stderr}"
    @ONLY
)

tpp_add_cli_integration_test(
    control_flow_error
    EXIT_CODE 1
    STDERR_FILE "${control_flow_error_stderr}"
    ARGS "${control_flow_error_source}"
)

tpp_add_cli_integration_test(
    suppress_dump_on_control_flow_error
    EXIT_CODE 1
    STDERR_FILE "${control_flow_error_stderr}"
    ARGS --dump-ast "${control_flow_error_source}"
)

tpp_add_cli_integration_test(
    suppress_emit_on_control_flow_error
    EXIT_CODE 1
    STDERR_FILE "${control_flow_error_stderr}"
    ARGS --emit-cpp "${control_flow_error_source}"
)

set(unreachable_warning_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/unreachable_warning.tpp"
)
set(unreachable_warning_stdout
    "${CMAKE_CURRENT_SOURCE_DIR}/data/unreachable_warning.expected.cpp"
)
set(unreachable_warning_stderr
    "${CMAKE_CURRENT_BINARY_DIR}/unreachable_warning.expected.stderr"
)
configure_file(
    data/unreachable_warning.expected.stderr.in
    "${unreachable_warning_stderr}"
    @ONLY
)

tpp_add_cli_integration_test(
    emit_cpp_with_unreachable_warning
    EXIT_CODE 0
    STDOUT_FILE "${unreachable_warning_stdout}"
    STDERR_FILE "${unreachable_warning_stderr}"
    ARGS --emit-cpp "${unreachable_warning_source}"
)

set(codegen_unsupported_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_unsupported.tpp"
)
set(codegen_unsupported_stderr
    "${CMAKE_CURRENT_BINARY_DIR}/codegen_unsupported.expected.stderr"
)
configure_file(
    data/codegen_unsupported.expected.stderr.in
    "${codegen_unsupported_stderr}"
    @ONLY
)

tpp_add_cli_integration_test(
    suppress_emit_on_codegen_error
    EXIT_CODE 1
    STDERR_FILE "${codegen_unsupported_stderr}"
    ARGS --emit-cpp "${codegen_unsupported_source}"
)

set(missing_source "${CMAKE_CURRENT_SOURCE_DIR}/data/does-not-exist.tpp")

tpp_add_cli_integration_test(
    missing_source
    EXIT_CODE 1
    STDERR "pseudo: error: cannot open '${missing_source}'\n"
    ARGS "${missing_source}"
)

find_program(TPP_GXX_EXECUTABLE NAMES g++)

if(TPP_GXX_EXECUTABLE)
    if(UNIX)
        add_test(
            NAME gpp_compiler.actual_gxx
            COMMAND
                "$<TARGET_FILE:gpp_compiler_tests>"
                --actual-gxx
                "${TPP_GXX_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/runtime/include"
        )
    endif()

    add_test(
        NAME codegen.smoke.gxx
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${codegen_functions_source}"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-smoke.cpp"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenSmoke.cmake"
    )

    add_test(
        NAME codegen.execute.strings
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${codegen_strings_source}"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-strings.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-strings-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_strings.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.string_input
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_string_input.tpp"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-string-input.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-string-input-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DSTDIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_string_input.stdin"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_string_input.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.vectors
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${codegen_vectors_source}"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-vectors.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-vectors-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_vectors.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.nested_vectors
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_nested_vectors.tpp"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-nested-vectors.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-nested-vectors-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_nested_vectors.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.integer_input
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_integer_input.tpp"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-integer-input.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-integer-input-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DSTDIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_integer_input.stdin"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_integer_input.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.runtime_io
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${codegen_runtime_io_source}"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-runtime-io.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-runtime-io-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DSTDIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_runtime_io.stdin"
            "-DEXPECTED_STDOUT_HEX=2d370a68656c6c6f0a510a380a747275650a66616c73650a4100420a"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.loop_ranges
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_loop_ranges.tpp"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-loop-ranges.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-loop-ranges-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_loop_ranges.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.nested_loops
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_nested_loops.tpp"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-nested-loops.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-nested-loops-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_nested_loops.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )

    add_test(
        NAME codegen.execute.foreach
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCLI=$<TARGET_FILE:pseudo>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_foreach.tpp"
            "-DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/codegen-foreach.cpp"
            "-DEXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/codegen-foreach-exe"
            "-DCXX=${TPP_GXX_EXECUTABLE}"
            "-DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include"
            "-DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/data/codegen_foreach.expected.stdout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCodegenExecution.cmake"
    )
endif()
