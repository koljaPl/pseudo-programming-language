foreach(required_variable CLI VALID_INPUT INVALID_INPUT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${CLI}" --emit-cpp "${VALID_INPUT}"
    RESULT_VARIABLE valid_first_exit
    OUTPUT_VARIABLE valid_first_stdout
    ERROR_VARIABLE valid_first_stderr
    TIMEOUT 10
)
execute_process(
    COMMAND "${CLI}" --emit-cpp "${VALID_INPUT}"
    RESULT_VARIABLE valid_second_exit
    OUTPUT_VARIABLE valid_second_stdout
    ERROR_VARIABLE valid_second_stderr
    TIMEOUT 10
)

if(NOT valid_first_exit EQUAL 0 OR NOT valid_second_exit EQUAL 0)
    message(
        FATAL_ERROR
        "repeated valid compilation failed: "
        "${valid_first_exit}, ${valid_second_exit}"
    )
endif()
if(valid_first_stdout STREQUAL "")
    message(FATAL_ERROR "valid --emit-cpp output is empty")
endif()
if(NOT valid_first_stdout STREQUAL valid_second_stdout)
    message(FATAL_ERROR "valid --emit-cpp output is not deterministic")
endif()
if(NOT valid_first_stderr STREQUAL valid_second_stderr)
    message(FATAL_ERROR "valid diagnostics are not deterministic")
endif()
if(NOT valid_first_stderr STREQUAL "")
    message(FATAL_ERROR "valid compilation unexpectedly wrote diagnostics")
endif()

execute_process(
    COMMAND "${CLI}" "${INVALID_INPUT}"
    RESULT_VARIABLE invalid_first_exit
    OUTPUT_VARIABLE invalid_first_stdout
    ERROR_VARIABLE invalid_first_stderr
    TIMEOUT 10
)
execute_process(
    COMMAND "${CLI}" "${INVALID_INPUT}"
    RESULT_VARIABLE invalid_second_exit
    OUTPUT_VARIABLE invalid_second_stdout
    ERROR_VARIABLE invalid_second_stderr
    TIMEOUT 10
)

if(NOT invalid_first_exit EQUAL 1 OR NOT invalid_second_exit EQUAL 1)
    message(
        FATAL_ERROR
        "repeated invalid compilation returned unexpected status: "
        "${invalid_first_exit}, ${invalid_second_exit}"
    )
endif()
if(NOT invalid_first_stdout STREQUAL ""
   OR NOT invalid_second_stdout STREQUAL "")
    message(FATAL_ERROR "invalid compilation produced stdout")
endif()
if(invalid_first_stderr STREQUAL "")
    message(FATAL_ERROR "invalid compilation produced no diagnostics")
endif()
if(NOT invalid_first_stderr STREQUAL invalid_second_stderr)
    message(FATAL_ERROR "invalid diagnostics are not deterministic")
endif()
