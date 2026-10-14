# libstdoc - expect the documented fatal status
#
# Runs one scenario of the heap abort test and requires the status the
# safe heap functions are documented to terminate with.  Anything else
# - a clean exit, a different status or a crash - fails the test, so a
# scenario that stops failing cannot pass unnoticed.
#
#     cmake -DEXECUTABLE=<program> -DSCENARIO=<name> -P CheckFatal.cmake
#
# 134 mirrors STDOC_HEAP_FATAL_STATUS in mem/heap.c: the conventional
# status of an aborted process (128 + SIGABRT).

execute_process(
    COMMAND "${EXECUTABLE}" "${SCENARIO}"
    RESULT_VARIABLE status
)

if(NOT status STREQUAL "134")
    message(FATAL_ERROR
        "scenario \"${SCENARIO}\" ended with status \"${status}\"; "
        "the documented fatal status is 134")
endif()
