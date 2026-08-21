foreach(
    required_variable
    CLI
    INPUT
    GENERATED
    EXECUTABLE
    CXX
    RUNTIME_INCLUDE
)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(has_expected_stdout_file FALSE)
if(DEFINED EXPECTED_STDOUT_FILE AND NOT EXPECTED_STDOUT_FILE STREQUAL "")
    set(has_expected_stdout_file TRUE)
endif()

set(has_expected_stdout_hex FALSE)
if(DEFINED EXPECTED_STDOUT_HEX AND NOT EXPECTED_STDOUT_HEX STREQUAL "")
    set(has_expected_stdout_hex TRUE)
endif()

if(has_expected_stdout_file AND has_expected_stdout_hex)
    message(
        FATAL_ERROR
        "only one of EXPECTED_STDOUT_FILE or EXPECTED_STDOUT_HEX may be set"
    )
endif()

if(NOT has_expected_stdout_file AND NOT has_expected_stdout_hex)
    message(
        FATAL_ERROR
        "EXPECTED_STDOUT_FILE or EXPECTED_STDOUT_HEX is required"
    )
endif()

execute_process(
    COMMAND "${CLI}" --emit-cpp "${INPUT}"
    RESULT_VARIABLE emit_exit
    OUTPUT_FILE "${GENERATED}"
    ERROR_VARIABLE emit_stderr
)

if(NOT emit_exit EQUAL 0)
    message(
        FATAL_ERROR
        "pseudo --emit-cpp failed with exit code ${emit_exit}\n${emit_stderr}"
    )
endif()

if(NOT emit_stderr STREQUAL "")
    message(FATAL_ERROR "pseudo --emit-cpp wrote to stderr\n${emit_stderr}")
endif()

execute_process(
    COMMAND
        "${CXX}"
        -std=c++20
        -Wall
        -Wextra
        -Wpedantic
        -I "${RUNTIME_INCLUDE}"
        "${GENERATED}"
        -o "${EXECUTABLE}"
    RESULT_VARIABLE compile_exit
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(NOT compile_exit EQUAL 0)
    message(
        FATAL_ERROR
        "g++ rejected generated C++ with exit code ${compile_exit}\n"
        "${compile_stdout}${compile_stderr}"
    )
endif()

if(NOT compile_stdout STREQUAL "" OR NOT compile_stderr STREQUAL "")
    message(
        FATAL_ERROR
        "g++ produced warnings or unexpected output\n"
        "${compile_stdout}${compile_stderr}"
    )
endif()

set(actual_stdout "${GENERATED}.stdout")
set(actual_stderr "${GENERATED}.stderr")

if(DEFINED STDIN_FILE AND NOT STDIN_FILE STREQUAL "")
    execute_process(
        COMMAND "${EXECUTABLE}"
        INPUT_FILE "${STDIN_FILE}"
        OUTPUT_FILE "${actual_stdout}"
        ERROR_FILE "${actual_stderr}"
        RESULT_VARIABLE execute_exit
        TIMEOUT 5
    )
else()
    execute_process(
        COMMAND "${EXECUTABLE}"
        OUTPUT_FILE "${actual_stdout}"
        ERROR_FILE "${actual_stderr}"
        RESULT_VARIABLE execute_exit
        TIMEOUT 5
    )
endif()

if(NOT execute_exit EQUAL 0)
    message(
        FATAL_ERROR
        "generated executable failed with exit code ${execute_exit}"
    )
endif()

file(SIZE "${actual_stderr}" stderr_size)
if(NOT stderr_size EQUAL 0)
    file(READ "${actual_stderr}" execute_stderr)
    message(
        FATAL_ERROR
        "generated executable wrote to stderr\n${execute_stderr}"
    )
endif()

if(has_expected_stdout_file)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E compare_files
            "${EXPECTED_STDOUT_FILE}"
            "${actual_stdout}"
        RESULT_VARIABLE compare_exit
    )

    if(NOT compare_exit EQUAL 0)
        message(
            FATAL_ERROR
            "generated executable stdout differs from "
            "${EXPECTED_STDOUT_FILE}"
        )
    endif()
else()
    file(READ "${actual_stdout}" actual_stdout_hex HEX)
    if(NOT actual_stdout_hex STREQUAL EXPECTED_STDOUT_HEX)
        message(
            FATAL_ERROR
            "generated executable stdout differs from expected hex\n"
            "expected: ${EXPECTED_STDOUT_HEX}\n"
            "actual:   ${actual_stdout_hex}"
        )
    endif()
endif()
