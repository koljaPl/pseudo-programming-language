foreach(
    required_variable
    CLI
    CLI_ARG_COUNT
    EXPECTED_EXIT
    EXPECTED_STDOUT
    EXPECTED_STDERR
)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(CLI_ARGS)

if(CLI_ARG_COUNT GREATER 0)
    math(EXPR last_argument "${CLI_ARG_COUNT} - 1")

    foreach(index RANGE "${last_argument}")
        set(argument_name "CLI_ARG_${index}")

        if(NOT DEFINED ${argument_name})
            message(FATAL_ERROR "${argument_name} is required")
        endif()

        list(APPEND CLI_ARGS "${${argument_name}}")
    endforeach()
endif()

execute_process(
    COMMAND "${CLI}" ${CLI_ARGS}
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
)

if(NOT "${actual_exit}" STREQUAL "${EXPECTED_EXIT}")
    message(
        FATAL_ERROR
        "Expected exit code ${EXPECTED_EXIT}, got ${actual_exit}"
    )
endif()

if(NOT "${actual_stdout}" STREQUAL "${EXPECTED_STDOUT}")
    message(
        FATAL_ERROR
        "Unexpected stdout\n"
        "--- expected ---\n${EXPECTED_STDOUT}"
        "--- actual ---\n${actual_stdout}"
    )
endif()

if(NOT "${actual_stderr}" STREQUAL "${EXPECTED_STDERR}")
    message(
        FATAL_ERROR
        "Unexpected stderr\n"
        "--- expected ---\n${EXPECTED_STDERR}"
        "--- actual ---\n${actual_stderr}"
    )
endif()
