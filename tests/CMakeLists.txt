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
    cmake_parse_arguments(TEST "" "EXIT_CODE;STDOUT;STDERR" "ARGS" ${ARGN})

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

set(valid_lexical_source
    "${CMAKE_CURRENT_SOURCE_DIR}/data/valid_lexical.tpp"
)

tpp_add_cli_integration_test(
    valid_lexical_source
    EXIT_CODE 0
    ARGS "${valid_lexical_source}"
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

set(missing_source "${CMAKE_CURRENT_SOURCE_DIR}/data/does-not-exist.tpp")

tpp_add_cli_integration_test(
    missing_source
    EXIT_CODE 1
    STDERR "pseudo: error: cannot open '${missing_source}'\n"
    ARGS "${missing_source}"
)
