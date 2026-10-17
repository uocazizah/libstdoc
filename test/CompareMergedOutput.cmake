# libstdoc - compare a test program's merged output with a golden file
#
# Usage:
#
#     cmake -DEXECUTABLE=<program> -DEXPECTED=<file> -DWORKING_DIR=<dir> \
#           -P CompareMergedOutput.cmake
#
# Both streams are sent to one file, the way a user sees them, so the
# golden file can pin the relative order of stdout and stderr and not
# only their contents.  The exit status of the program is checked as
# well, so a test that dies without printing anything still fails.

foreach(required EXECUTABLE EXPECTED WORKING_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "CompareMergedOutput.cmake: ${required} is not set")
    endif()
endforeach()

if(NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR
        "CompareMergedOutput.cmake: no such golden file: ${EXPECTED}")
endif()

file(MAKE_DIRECTORY "${WORKING_DIR}")

set(merged "${WORKING_DIR}/merged-output.txt")
file(REMOVE "${merged}")

# A shell performs the redirection.  "exec" replaces the shell with the
# program, so the status reported here is the program's own; the two
# paths are passed as positional arguments so that neither of them is
# ever parsed as part of the script.
execute_process(
    COMMAND /bin/sh -c "exec \"$0\" > \"$1\" 2>&1" "${EXECUTABLE}" "${merged}"
    RESULT_VARIABLE status
)

if(NOT status EQUAL 0)
    message(FATAL_ERROR "${EXECUTABLE} exited with status ${status}")
endif()

file(READ "${merged}" actual)
file(READ "${EXPECTED}" expected)

if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
        "${EXECUTABLE} does not match ${EXPECTED} once the two streams "
        "are merged\n"
        "--- expected ---\n${expected}\n"
        "--- actual ---\n${actual}")
endif()
