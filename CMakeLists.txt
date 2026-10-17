# libstdoc - build description
#
# The library is freestanding: it is built without any host C library
# and provides its own program entry point.  The test programs are
# linked the same way, so a build of this project is a complete
# demonstration that the library works on its own rather than a hosted
# build that happens to contain it.
#
# Out-of-source builds only:
#
#     cmake -S . -B build
#     cmake --build build
#     ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.16)

# The version is that of the library as a package.  The beta status of
# this point in the history - 1.0.0-beta - is recorded in NEWS; CMake
# versions carry no suffix, and the interface may still change before
# the 1.0.0 final release.
project(libstdoc VERSION 1.0.0 LANGUAGES C ASM)

# ISO C17 for every C target in the project, tests included, so that no
# target is ever compiled against a dialect the library does not
# support.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Target selection.
#
# The platform-specific pieces are chosen here and nowhere else.  An
# operating system or architecture that is not implemented is rejected
# outright: silently picking an implementation that does not match the
# target would produce a library that builds and then misbehaves.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "libstdoc: unsupported operating system \"${CMAKE_SYSTEM_NAME}\"; "
        "only Linux is implemented")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(STDOC_PROCESSOR "aarch64")
else()
    message(FATAL_ERROR
        "libstdoc: unsupported target architecture \"${CMAKE_SYSTEM_PROCESSOR}\"; "
        "only aarch64 is implemented")
endif()

# Compiler selection.
#
# The flags below are GNU-style.  They are only used after the compiler
# has been identified, never assumed to exist.
if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    # Strict warnings for the library and the tests alike.
    #
    # The baseline below every build gets: the usual -Wall -Wextra
    # -Wpedantic set, and then the warnings that matter most for a
    # library that hands raw memory and formatted output to its
    # callers.  -Wconversion and -Wsign-conversion catch a narrowing
    # that would silently truncate a size; -Wshadow and -Wcast-qual
    # catch a name or a cast that hides a mistake; -Wstrict-prototypes
    # and -Wmissing-prototypes keep the internal functions declared the
    # way the project declares them; -Wswitch-default, -Wundef,
    # -Wdouble-promotion and -Wvla cover the remaining slips that a
    # freestanding build cannot afford.
    #
    # Every flag here is understood by both GNU C and Clang, so the
    # same set can be used for either without a check.
    set(STDOC_C_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wcast-qual
        -Wundef
        -Wswitch-default
        -Wdouble-promotion
        -Wvla)

    # The library is compiled as freestanding code: it may not assume a
    # host C library, may not use compiler builtins in place of its own
    # functions, and may not depend on the stack protector, which needs
    # a runtime symbol that does not exist here.
    set(STDOC_FREESTANDING_FLAGS -ffreestanding -fno-builtin
        -fno-stack-protector)

    # Test programs keep the runtime assumptions that actually matter
    # for a freestanding link: no stack protector, whose failure
    # handler does not exist here, and no compiler builtins, so a call
    # can never be rewritten into one this library does not provide.
    #
    # -ffreestanding is deliberately left out.  It makes clang treat
    # main() as an ordinary function: its name is mangled and the
    # implicit "return 0" of a main that runs off the end disappears,
    # so the program would exit with whatever the last call left in
    # the return register.
    set(STDOC_TEST_FLAGS -fno-builtin -fno-stack-protector)

    # Linking the tests with no startup files and no libraries is what
    # makes the library's own _start and string helpers the only ones
    # available, which is the point of the whole exercise.
    #
    # -static is what keeps the result a plain non-PIE executable: both
    # the GNU and the Clang driver already imply -no-pie for a static
    # link, and passing -no-pie as well would only make Clang report it
    # as an unused argument on every link.
    set(STDOC_LINK_OPTIONS -nostdlib -static)
else()
    message(FATAL_ERROR
        "libstdoc: compiler \"${CMAKE_C_COMPILER_ID}\" is not supported; "
        "GNU C or Clang is required")
endif()

# A C++ compiler is only needed by the C++ compatibility test.
include(CheckLanguage)
check_language(CXX)

if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(STDOC_HAVE_CXX ON)
else()
    message(STATUS "libstdoc: no C++ compiler found, skipping the C++ test")
    set(STDOC_HAVE_CXX OFF)
endif()

# The library.
#
# assembly/start_aarch64.S is part of the archive because the entry
# point belongs to the runtime this library provides.  The test
# executables still compile it into themselves so that linking a test
# never depends on the linker recovering a symbol from an archive.
#
# mem/heap.c is the dynamic memory allocator: its arena is a static
# object whose size is fixed here, so a build that wants a different
# heap sets STDOC_HEAP_SIZE rather than editing a header.
set(STDOC_HEAP_SIZE "4096" CACHE STRING
    "size in bytes of the libstdoc heap arena")

if(NOT STDOC_HEAP_SIZE MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "libstdoc: STDOC_HEAP_SIZE \"${STDOC_HEAP_SIZE}\" is not a "
        "whole number of bytes")
endif()

# The arena has to be able to hold one block, which on this target is
# four alignment units of 16 bytes.  The authoritative check is the
# static assertion in mem/heap.c, which knows the alignment of the
# actual target; rejecting the values that can never compile here means
# such a build is never started, and the message names what is wrong
# instead of leaving three compiler errors to explain it.
if(STDOC_HEAP_SIZE LESS 64)
    message(FATAL_ERROR
        "libstdoc: STDOC_HEAP_SIZE \"${STDOC_HEAP_SIZE}\" is smaller "
        "than the smallest arena libstdoc can build with (64 bytes); "
        "the arena must hold at least one heap block")
endif()

add_library(stdoc STATIC
    assembly/start_aarch64.S
    assembly/syscall.S
    io/syscall_linux.c
    io/file.c
    io/string.c
    io/vfprintf.c
    io/printf.c
    io/fprintf.c
    mem/heap.c
)

set_target_properties(stdoc PROPERTIES OUTPUT_NAME stdoc)

# Consumers of the build tree use the same name they get once the
# library is installed, so a project can be pointed at either without
# being edited.
add_library(libstdoc::stdoc ALIAS stdoc)

target_compile_options(stdoc PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${STDOC_C_WARNINGS}>
    $<$<COMPILE_LANGUAGE:C>:${STDOC_FREESTANDING_FLAGS}>
)

# The arena size reaches the allocator as a definition rather than as
# an edit to a header, so the configured value is the only one the
# library is built with.
target_compile_definitions(stdoc PRIVATE
    STDOC_HEAP_SIZE=${STDOC_HEAP_SIZE}
)

target_include_directories(stdoc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/io
        ${CMAKE_CURRENT_SOURCE_DIR}/mem
)

option(STDOC_BUILD_TESTS "build the libstdoc test programs" ON)

if(STDOC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Installation.
#
# Four things are installed: the archive, the headers, a CMake package
# and a pkg-config file.  The last two exist so that another project
# can find the library without being told where it ended up.
#
# The default prefix is /usr/local, which normally needs the privileges
# of the root user:
#
#     cmake -S . -B build                    # configure with the default
#     cmake --install build                  # needs sudo
#
# A per-user installation needs no privileges and puts everything
# below the home directory:
#
#     cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
#     cmake --install build
#
# The prefix belongs on the configure line, not on the install line:
# the paths written into libstdoc.pc are fixed when the project is
# configured, so "cmake --install build --prefix ..." moves the files
# but leaves the metadata pointing at the prefix that was configured.
#
# A program then links against it with either of:
#
#     cc my_program.c $(pkg-config --cflags --libs libstdoc)
#     cmake with -DCMAKE_PREFIX_PATH=<prefix> and find_package(libstdoc)
#
# A prefix outside the search path of pkg-config, such as the home
# directory, is not looked in by default:
#
#     PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig"
#
# Note that the library is freestanding by design: a program linked
# without a C runtime needs the entry point that libstdoc provides, and
# one linked with a normal C runtime gets it from there instead.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(STDOC_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/libstdoc)
set(STDOC_INSTALL_PKGCONFIGDIR ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

install(TARGETS stdoc
    EXPORT libstdocTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    include/libstdoc.h
    include/libstdoc.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT libstdocTargets
    FILE libstdocTargets.cmake
    NAMESPACE libstdoc::
    DESTINATION ${STDOC_INSTALL_CMAKEDIR}
)

configure_package_config_file(
    etc/libstdoc-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libstdocConfig.cmake
    INSTALL_DESTINATION ${STDOC_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libstdocConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libstdocConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libstdocConfigVersion.cmake
    DESTINATION ${STDOC_INSTALL_CMAKEDIR}
)

configure_file(
    etc/libstdoc.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libstdoc.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libstdoc.pc
    DESTINATION ${STDOC_INSTALL_PKGCONFIGDIR}
)
