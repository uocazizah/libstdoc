# libstdoc - check that one large write arrives whole and is reported as a success
#
# Usage:
#
#     cmake -DEXECUTABLE=<program> -DWORKING_DIR=<dir> -DEXPECTED_SIZE=<bytes> \
#           -P CheckLargeWrite.cmake
#
# The program hands one buffer, much larger than the stream buffer and
# larger than a pipe can hold, to the write wrapper in a single call and
# exits non-zero if the wrapper reports a failure.
#
# Three things are checked, on both destinations:
#
#   1. the wrapper reported success - a completed write of a whole buffer
#      arrives as a count, and a count is not a kernel error however
#      large it is;
#   2. every byte arrived, which the length alone does not prove if the
#      buffer was written twice or shifted;
#   3. the bytes are the ones the program meant to write, compared with
#      the pattern the program produces rather than with a golden file of
#      two hundred thousand bytes.
#
# A regular file and a pipe are different kernel paths - a file takes the
# buffer as it is, a pipe has to carry it out in pipe-sized pieces while
# the caller is blocked in the call - so a wrapper that mishandled either
# one fails here.

foreach(required EXECUTABLE WORKING_DIR EXPECTED_SIZE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "CheckLargeWrite.cmake: ${required} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORKING_DIR}")

set(direct "${WORKING_DIR}/direct.bin")
set(piped  "${WORKING_DIR}/piped.bin")

file(REMOVE "${direct}" "${piped}")

# The pattern the program fills its buffer with: the digits 0 to 9,
# repeating.
math(EXPR repetitions "${EXPECTED_SIZE} / 10")
string(REPEAT "0123456789" ${repetitions} pattern)
string(LENGTH "${pattern}" pattern_length)

if(NOT pattern_length EQUAL EXPECTED_SIZE)
    message(FATAL_ERROR
        "CheckLargeWrite.cmake: EXPECTED_SIZE must be a multiple of ten")
endif()

function(check_one_destination path label)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "the ${label} run produced no output at all")
    endif()

    file(SIZE "${path}" size)

    if(NOT size EQUAL EXPECTED_SIZE)
        message(FATAL_ERROR
            "the ${label} run produced ${size} bytes instead of "
            "${EXPECTED_SIZE}")
    endif()

    file(READ "${path}" content)

    if(NOT content STREQUAL pattern)
        message(FATAL_ERROR
            "the ${label} run produced the right number of bytes but not "
            "the right bytes")
    endif()
endfunction()

# Straight to a regular file.  The status of the program is the status of
# this command, so a wrapper that reported the count as an error fails
# here even though the file itself would be complete.
execute_process(
    COMMAND /bin/sh -c "exec \"$0\" > \"$1\"" "${EXECUTABLE}" "${direct}"
    RESULT_VARIABLE direct_status
)

if(NOT direct_status EQUAL 0)
    message(FATAL_ERROR
        "the file run exited with status ${direct_status}: the wrapper did "
        "not report the completed write as a success")
endif()

check_one_destination("${direct}" "file")

# Through a pipe.  A pipeline reports the status of its last command, so
# what is checked here is the bytes; the program's own status is what the
# file run above checks.
execute_process(
    COMMAND /bin/sh -c "exec \"$0\" | cat > \"$1\"" "${EXECUTABLE}" "${piped}"
    RESULT_VARIABLE pipe_status
)

if(NOT pipe_status EQUAL 0)
    message(FATAL_ERROR "the pipe run exited with status ${pipe_status}")
endif()

check_one_destination("${piped}" "pipe")
