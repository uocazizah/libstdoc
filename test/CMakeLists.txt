# libstdoc - test programs
#
# Every C test is a freestanding executable: it is linked without a C
# runtime and without any library other than libstdoc, so the only
# printf, the only _start and the only string helpers it can use are
# the ones this project provides.

set(STDOC_START_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../assembly/start_aarch64.S)
set(STDOC_IO_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../io)
set(STDOC_MEM_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mem)

# The warning set of the tests is the one the library is built with, so
# a test can never be sloppier than the code it exercises.
#
# The C++ test gets the same set minus the two options that only exist
# for C: GNU C++ rejects them outright, and a warning about the warning
# flag would fail the build before compiling anything.
set(STDOC_TEST_WARNINGS ${STDOC_C_WARNINGS})
set(STDOC_TEST_WARNINGS_CXX ${STDOC_C_WARNINGS})
list(REMOVE_ITEM STDOC_TEST_WARNINGS_CXX -Wstrict-prototypes)
list(REMOVE_ITEM STDOC_TEST_WARNINGS_CXX -Wmissing-prototypes)

# The conversion suite and the error suite deliberately use format
# strings that "-Wformat" rejects: a width literal that does not fit in
# an "int", an empty format string, the '0' flag with a string, a
# precision on %p, a '+' on an unsigned conversion, a '#' on %d.  Those
# are behaviours this library defines on purpose - pinned against the
# reference C library and written down in docs/formatting - and the
# tests exist to keep them that way, so the compiler check is switched
# off for those two translation units alone.
#
# Nothing is lost for the rest of the suite: the attribute on the
# declarations in <libstdoc.h> is what performs the check, and every
# other test, along with the library's own diagnostics, keeps it.
set(STDOC_FORMAT_TEST_WARNINGS ${STDOC_C_WARNINGS} -Wno-format)

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
        $<$<COMPILE_LANGUAGE:CXX>:${STDOC_TEST_WARNINGS_CXX}>
        $<$<COMPILE_LANGUAGE:C>:${STDOC_TEST_FLAGS}>
    )
    target_compile_definitions(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:STDOC_HEAP_SIZE=${STDOC_HEAP_SIZE}>
    )
    target_link_options(${target} PRIVATE ${STDOC_LINK_OPTIONS})
endfunction()

# index.c: the smoke test this repository started with.
stdoc_add_test_program(test_index "${STDOC_TEST_WARNINGS}" index.c)
stdoc_add_output_test(index_output test_index expected-index.txt)

# format.c: the formatting engine, checked against golden output that
# was produced by a reference C library from the same source.
stdoc_add_test_program(test_format "${STDOC_FORMAT_TEST_WARNINGS}" format.c)
stdoc_add_output_test(format_output test_format expected-format.txt)

# differential.c: the same source compiled twice - once linked against
# libstdoc and once against the host C library - and compared case by
# case at test time.  The golden file above is a snapshot; this keeps a
# reference library in the comparison, and its cases are built at run
# time, so the sweep can be far wider than a file of literals.
stdoc_add_test_program(test_differential "${STDOC_FORMAT_TEST_WARNINGS}"
    differential.c)

add_executable(test_differential_reference differential.c)
target_compile_definitions(test_differential_reference PRIVATE STDOC_REFERENCE)
target_compile_options(test_differential_reference PRIVATE
    ${STDOC_FORMAT_TEST_WARNINGS})

add_test(NAME differential
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE_A=$<TARGET_FILE:test_differential>
        -DEXECUTABLE_B=$<TARGET_FILE:test_differential_reference>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CompareTwo.cmake
)

# errors.c: the failure paths.  It checks its own return codes instead
# of comparing output, so it needs no golden file.  The internal header
# is used on purpose: a stream with a bad descriptor cannot be obtained
# through the public interface, and the sticky-error behaviour is worth
# testing.
stdoc_add_test_program(test_errors "${STDOC_FORMAT_TEST_WARNINGS}" errors.c)
target_include_directories(test_errors PRIVATE ${STDOC_IO_SOURCE_DIR})
add_test(NAME error_handling COMMAND test_errors)

# stream.c: the staging buffer, the discard, and the relative order of
# stdout and stderr.  Its golden file is the two streams merged into
# one, so it pins where a diagnostic lands and not only its text.
stdoc_add_test_program(test_stream "${STDOC_TEST_WARNINGS}" stream.c)
target_include_directories(test_stream PRIVATE ${STDOC_IO_SOURCE_DIR})

add_test(NAME stream_output
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:test_stream>
        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected-stream.txt
        -DWORKING_DIR=${CMAKE_CURRENT_BINARY_DIR}/stream-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CompareMergedOutput.cmake
)

# largewrite.c: one buffer, larger than the stream buffer and larger
# than a pipe can hold, written through the wrapper in a single call.
# Its status, its length and its bytes are checked on a regular file and
# through a pipe, which is what pins the count-versus-error boundary in
# stdoc_kernel_result and proves that a transfer the kernel carries out
# in pieces arrives whole.
stdoc_add_test_program(test_largewrite "${STDOC_TEST_WARNINGS}" largewrite.c)
target_include_directories(test_largewrite PRIVATE ${STDOC_IO_SOURCE_DIR})

add_test(NAME large_write_is_reported
    COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:test_largewrite>
        -DWORKING_DIR=${CMAKE_CURRENT_BINARY_DIR}/large-write-test
        -DEXPECTED_SIZE=200000
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckLargeWrite.cmake
)

# heap.c: the allocator, its safe variants and the leak accounting.
# Every scenario allocates only a few kilobytes, so anything left
# behind stays that small, and the test fails unless the live byte and
# block counts are back at zero after each one.
stdoc_add_test_program(test_heap "${STDOC_TEST_WARNINGS}" heap.c)
target_include_directories(test_heap PRIVATE
    ${STDOC_IO_SOURCE_DIR}
    ${STDOC_MEM_SOURCE_DIR}
)
add_test(NAME heap_checks COMMAND test_heap)

# heap_abort.c: the fail-fast paths of the safe functions.  Each
# scenario has to end with the documented fatal status 134, which
# CheckFatal.cmake asserts; a scenario that exits cleanly or crashes
# fails instead of passing unnoticed.
stdoc_add_test_program(test_heap_abort "${STDOC_TEST_WARNINGS}" heap_abort.c)
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
    # The C++ test proves that <libstdoc.hpp> is usable from C++ and
    # that the C symbols link under their unmangled names.
    #
    # It is deliberately not built like the C tests: a C++ program needs
    # the C++ runtime for its own start-up, so it is linked as an
    # ordinary hosted program against the host C++ toolchain, with the
    # library archived into it.  It also must not be compiled with
    # -ffreestanding, which would make clang mangle main() and leave the
    # program without an entry point.  Only the warning policy is shared.
    add_executable(test_index_cxx index.cxx)
    target_link_libraries(test_index_cxx PRIVATE stdoc)
    target_compile_options(test_index_cxx PRIVATE ${STDOC_TEST_WARNINGS_CXX})
    stdoc_add_output_test(index_cxx_output test_index_cxx expected-index.txt)
endif()

# install_and_consume: libstdoc is configured, built and installed into a
# scratch prefix, and a separate project is then built against that
# prefix with find_package.  The install rules, the package metadata and
# the include directory carried by the imported target are therefore
# exercised on every run instead of only when someone installs the
# library by hand.  InstallTest.cmake does the work and explains what it
# checks.
#
# The library directory name is only known once GNUInstallDirs has run,
# which happens after this directory is added, so it is included here as
# well; the module is idempotent and the value it computes is the same.
include(GNUInstallDirs)

set(STDOC_INSTALL_TEST_ARGS
    -DLIBSTDOC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer
    -DWORKING_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-test
    -DLIBDIR=${CMAKE_INSTALL_LIBDIR}
    -DC_COMPILER=${CMAKE_C_COMPILER}
    -DHEAP_SIZE=${STDOC_HEAP_SIZE}
)

if(CMAKE_CXX_COMPILER)
    list(APPEND STDOC_INSTALL_TEST_ARGS -DCXX_COMPILER=${CMAKE_CXX_COMPILER})
endif()

add_test(NAME install_and_consume
    COMMAND ${CMAKE_COMMAND} ${STDOC_INSTALL_TEST_ARGS}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/InstallTest.cmake
)
