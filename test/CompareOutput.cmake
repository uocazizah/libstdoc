# libstdoc - compare a test program's output with a golden file
#
# Usage:
#
#     cmake -DEXECUTABLE=<program> -EXPECTED=<file> -P CompareOutput.cmake
#
# The exit status of the program is checked as well as its output, so a
# test that fails without printing anything is still reported.

if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "CompareOutput.cmake: EXECUTABLE is not set")
endif()

if(NOT DEFINED EXPECTED)
    message(FATAL_ERROR "CompareOutput.cmake: EXPECTED is not set")
endif()

if(NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR "CompareOutput.cmake: no such golden file: ${EXPECTED}")
endif()

execute_process(
    COMMAND "${EXECUTABLE}"
    OUTPUT_VARIABLE actual
    RESULT_VARIABLE status
)

if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "${EXECUTABLE} exited with status ${status}\noutput:\n${actual}")
endif()

file(READ "${EXPECTED}" expected)

if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
        "${EXECUTABLE} does not match ${EXPECTED}\n"
        "--- expected ---\n${expected}"
        "--- actual ---\n${actual}"
        "----------------")
endif()
