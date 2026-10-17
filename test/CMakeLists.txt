# libstdoc - test programs
#
# Every test is a freestanding executable: it is linked without a C
# runtime and without any library other than libstdoc, so the only
# printf, the only _start and the only string helpers it can use are
# the ones this project provides.

set(STDOC_START_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../assembly/start_aarch64.S)
set(STDOC_IO_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../io)
set(STDOC_MEM_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mem)

# The two test programs that came with this repository predate the
# strict warning policy and are built without -Werror.  The tests added
# here are built with the full policy.
set(STDOC_LEGACY_TEST_WARNINGS -Wall -Wextra -Wpedantic)

# Compare the standard output of an executable with a golden file.
#
# The comparison is done by CompareOutput.cmake rather than by a shell
# so that the tests behave the same way on every host.
function(stdoc_add_output_test name target golden)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${target}>
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/${golden}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/CompareOutput.cmake
    )
endfunction()

# Build one freestanding test program.
#
# The entry point is compiled into every test so that linking never
# depends on the linker recovering a symbol from an archive.  The
# configured arena size reaches the test the same way it reaches the
# library, so a test never silently builds against the default of the
# internal header while the library was built with another value.
function(stdoc_add_test_program target warnings)
    add_executable(${target} ${ARGN} ${STDOC_START_SOURCE})
    target_link_libraries(${target} PRIVATE stdoc)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${warnings}>
        $<$<COMPILE_LANGUAGE:C>:${STDOC_TEST_FLAGS}>
    )
    target_compile_definitions(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:STDOC_HEAP_SIZE=${STDOC_HEAP_SIZE}>
    )
    target_link_options(${target} PRIVATE ${STDOC_LINK_OPTIONS})
endfunction()

# index.c: the original smoke test of this repository, kept unchanged
# apart from what the strict warning policy needs.
stdoc_add_test_program(test_index "${STDOC_LEGACY_TEST_WARNINGS}" index.c)
stdoc_add_output_test(index_output test_index expected-index.txt)

# format.c: the formatting engine, checked against golden output that
# was produced by a reference C library from the same source.
stdoc_add_test_program(test_format "${STDOC_C_WARNINGS}" format.c)
stdoc_add_output_test(format_output test_format expected-format.txt)

# errors.c: the failure paths.  It checks its own return codes instead
# of comparing output, so it needs no golden file.  The internal header
# is used on purpose: a stream with a bad descriptor cannot be obtained
# through the public interface, and the sticky-error behaviour is worth
# testing.
stdoc_add_test_program(test_errors "${STDOC_C_WARNINGS}" errors.c)
target_include_directories(test_errors PRIVATE ${STDOC_IO_SOURCE_DIR})
add_test(NAME error_handling COMMAND test_errors)

# heap.c: the allocator, its safe variants and the leak accounting.
# Every scenario allocates only a few kilobytes, so anything left
# behind stays that small, and the test fails unless the live byte and
# block counts are back at zero after each one.
stdoc_add_test_program(test_heap "${STDOC_C_WARNINGS}" heap.c)
target_include_directories(test_heap PRIVATE
    ${STDOC_IO_SOURCE_DIR}
    ${STDOC_MEM_SOURCE_DIR}
)
add_test(NAME heap_checks COMMAND test_heap)

# heap_abort.c: the fail-fast paths of the safe functions.  Each
# scenario has to end with the documented fatal status 134, which
# CheckFatal.cmake asserts; a scenario that exits cleanly or crashes
# fails instead of passing unnoticed.
stdoc_add_test_program(test_heap_abort "${STDOC_C_WARNINGS}" heap_abort.c)
target_include_directories(test_heap_abort PRIVATE
    ${STDOC_IO_SOURCE_DIR}
    ${STDOC_MEM_SOURCE_DIR}
)

foreach(scenario overflow oom double_free invalid_pointer)
    add_test(NAME heap_abort_${scenario}
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:test_heap_abort>
            -DSCENARIO=${scenario}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckFatal.cmake
    )
endforeach()

if(STDOC_HAVE_CXX)
    # The C++ test proves that the header is usable from C++ and that
    # the C symbols link, which needs neither freestanding compilation
    # nor a freestanding link.  It is built as an ordinary hosted
    # program, exactly as the continuous integration of this repository
    # always did.
    #
    # It must not be compiled with -ffreestanding: clang then treats
    # main() as an ordinary function and mangles its name, so no
    # program can find its entry point.
    add_executable(test_index_cxx index.cxx)
    target_link_libraries(test_index_cxx PRIVATE stdoc)
    stdoc_add_output_test(index_cxx_output test_index_cxx expected-index.txt)
endif()
