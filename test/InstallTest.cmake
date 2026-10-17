# libstdoc - install the library and build a program against it
#
# Usage:
#
#     cmake -DLIBSTDOC_SOURCE_DIR=<source> -DCONSUMER_SOURCE_DIR=<source>
#           -DWORKING_DIR=<scratch> -DLIBDIR=<libdir>
#           -DC_COMPILER=<cc> -DCXX_COMPILER=<cxx> -DHEAP_SIZE=<bytes>
#           -P InstallTest.cmake
#
# Everything happens under WORKING_DIR, which is thrown away and
# rebuilt, so the test never touches the build tree it was started from
# and can be run as many times as wanted.
#
# What is checked, in order:
#
#   1. libstdoc configures, builds and installs into a scratch prefix;
#   2. the headers, the archive and the CMake package files are where
#      the install rules promised to put them;
#   3. a separate project finds the package with find_package, links
#      against the imported target and runs;
#   4. pkg-config, when it is installed, describes the same package.
#
# The failure of any step stops the test: a partially installed package
# is not a pass.

foreach(required LIBSTDOC_SOURCE_DIR CONSUMER_SOURCE_DIR WORKING_DIR LIBDIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "InstallTest.cmake: ${required} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORKING_DIR}")
file(MAKE_DIRECTORY "${WORKING_DIR}")

set(prefix    "${WORKING_DIR}/prefix")
set(lib_build "${WORKING_DIR}/lib-build")
set(app_build "${WORKING_DIR}/app-build")

# A helper that runs a command and stops the test with its output when
# the command fails.
function(run_or_fail)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE  error
    )

    if(NOT status EQUAL 0)
        string(JOIN " " command_line ${ARGN})
        message(FATAL_ERROR
            "command failed with status ${status}:\n  ${command_line}\n"
            "${output}${error}")
    endif()
endfunction()

set(configure_lib
    "${CMAKE_COMMAND}"
    -S "${LIBSTDOC_SOURCE_DIR}"
    -B "${lib_build}"
    "-DCMAKE_INSTALL_PREFIX=${prefix}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}"
    "-DSTDOC_HEAP_SIZE=${HEAP_SIZE}"
)

# A build machine without a C++ compiler is a supported configuration of
# this project, so the nested build is only told about one when there is
# one; passing an empty compiler would be worse than not passing it.
if(CXX_COMPILER)
    list(APPEND configure_lib "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()

run_or_fail(${configure_lib})

run_or_fail("${CMAKE_COMMAND}" --build "${lib_build}" --parallel)
run_or_fail("${CMAKE_COMMAND}" --install "${lib_build}")

# The installed files.  The include directory and the library directory
# are the ones GNUInstallDirs chose for this host, which is what the
# install rules use.
foreach(header libstdoc.h libstdoc.hpp)
    if(NOT EXISTS "${prefix}/include/${header}")
        message(FATAL_ERROR "the installed package has no include/${header}")
    endif()
endforeach()

if(NOT EXISTS "${prefix}/${LIBDIR}/libstdoc.a")
    message(FATAL_ERROR "the installed package has no ${LIBDIR}/libstdoc.a")
endif()

foreach(metadata libstdocConfig.cmake libstdocConfigVersion.cmake
                 libstdocTargets.cmake)
    if(NOT EXISTS "${prefix}/${LIBDIR}/cmake/libstdoc/${metadata}")
        message(FATAL_ERROR
            "the installed package has no ${LIBDIR}/cmake/libstdoc/${metadata}")
    endif()
endforeach()

# A consumer project against the installed prefix, without a single
# libstdoc path spelled out in it.
run_or_fail(
    "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${app_build}"
    "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}"
)

run_or_fail("${CMAKE_COMMAND}" --build "${app_build}")
execute_process(
    COMMAND "${app_build}/consumer"
    RESULT_VARIABLE consumer_status
    OUTPUT_VARIABLE consumer_output
    ERROR_VARIABLE  consumer_error
)

if(NOT consumer_status EQUAL 0)
    message(FATAL_ERROR
        "the consumer exited with status ${consumer_status}\n"
        "${consumer_error}")
endif()

if(NOT consumer_output STREQUAL "consumed libstdoc\n")
    message(FATAL_ERROR
        "the consumer printed something unexpected:\n${consumer_output}")
endif()

# pkg-config, when the host has it, has to describe the same package.
find_program(PKG_CONFIG_EXECUTABLE pkg-config)

if(PKG_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "PKG_CONFIG_PATH=${prefix}/${LIBDIR}/pkgconfig"
            "${PKG_CONFIG_EXECUTABLE}" --cflags --libs libstdoc
        RESULT_VARIABLE pkg_status
        OUTPUT_VARIABLE pkg_output
        ERROR_VARIABLE  pkg_error
    )

    if(NOT pkg_status EQUAL 0)
        message(FATAL_ERROR
            "pkg-config could not describe libstdoc:\n${pkg_error}")
    endif()

    if(NOT pkg_output MATCHES "-lstdoc")
        message(FATAL_ERROR
            "pkg-config libstdoc does not name the library:\n${pkg_output}")
    endif()
endif()
