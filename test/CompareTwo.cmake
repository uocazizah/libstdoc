# libstdoc - compare the output of two test programs
#
# Usage:
#
#     cmake -DEXECUTABLE_A=<libstdoc build> -DEXECUTABLE_B=<reference build> \
#           -P CompareTwo.cmake
#
# Both programs have to exit with status 0 and to print exactly the same
# bytes.  When they do not, the first line that differs is reported, so
# a failure names the case instead of dumping two long outputs.
#
# Lines are split on the newline, which turns each output into a CMake
# list; the differential test writes no semicolon precisely so that the
# split can be done this way.  The verdict itself does not depend on it:
# a mismatch in the number of lines is caught first.

if(NOT DEFINED EXECUTABLE_A)
    message(FATAL_ERROR "CompareTwo.cmake: EXECUTABLE_A is not set")
endif()

if(NOT DEFINED EXECUTABLE_B)
    message(FATAL_ERROR "CompareTwo.cmake: EXECUTABLE_B is not set")
endif()

execute_process(
    COMMAND "${EXECUTABLE_A}"
    OUTPUT_VARIABLE output_a
    RESULT_VARIABLE status_a
)

execute_process(
    COMMAND "${EXECUTABLE_B}"
    OUTPUT_VARIABLE output_b
    RESULT_VARIABLE status_b
)

if(NOT status_a EQUAL 0)
    message(FATAL_ERROR
        "${EXECUTABLE_A} exited with status ${status_a}")
endif()

if(NOT status_b EQUAL 0)
    message(FATAL_ERROR
        "${EXECUTABLE_B} exited with status ${status_b}")
endif()

if(output_a STREQUAL output_b)
    return()
endif()

string(REPLACE "\n" ";" lines_a "${output_a}")
string(REPLACE "\n" ";" lines_b "${output_b}")

list(LENGTH lines_a count_a)
list(LENGTH lines_b count_b)

if(NOT count_a EQUAL count_b)
    message(FATAL_ERROR
        "the two programs printed a different number of lines: "
        "${count_a} from ${EXECUTABLE_A} against ${count_b} from "
        "${EXECUTABLE_B}")
endif()

set(index 0)

foreach(line_a IN LISTS lines_a)
    list(GET lines_b ${index} line_b)

    if(NOT line_a STREQUAL line_b)
        math(EXPR line_number "${index} + 1")
        message(FATAL_ERROR
            "line ${line_number} differs\n"
            "  libstdoc:  ${line_a}\n"
            "  reference: ${line_b}")
    endif()

    math(EXPR index "${index} + 1")
endforeach()

# The loops above compare every line, so this is only reachable when the
# outputs differ in a way the split could not see.
message(FATAL_ERROR "the two outputs differ but no line does")
