foreach(required_variable CLI INPUT GENERATED CXX)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

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
        -fsyntax-only
        "${GENERATED}"
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
