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

# CMake 3.20 is the first release that knows C++23, which the library
# implementation is written in; an older CMake would reject the
# standard instead of building the project.
cmake_minimum_required(VERSION 3.20)

# The version is that of the library as a package.  The beta status of
# this point in the history - 2.0.1-beta - is recorded in NEWS; CMake
# versions carry no suffix, and the interface may still change before
# the final release.
project(libstdoc VERSION 2.0.1 LANGUAGES C CXX ASM)

# A build without a chosen type gets no optimisation flags at all, which
# is a poor default for a library whose release notes describe its
# performance.  RelWithDebInfo keeps the ordinary -O2 code generation and
# adds debug information, so a crash can still be read with a debugger;
# an explicit -DCMAKE_BUILD_TYPE=Debug or Release overrides it.  A
# multi-configuration generator chooses the type at build time instead,
# so the cache variable does not apply there.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "libstdoc: no build type given; using RelWithDebInfo")
endif()

# The public interface is ISO C17 and so are the test programs that
# consume it as C, so every C target in the project is compiled as C17
# with extensions off: a public header that only compiles as a GNU
# dialect would break a consumer that uses the standard one.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ISO C++23 for every C++ target in the project: the library
# implementation is written in C++23, and a compiler that does not
# support the standard is rejected at configure time rather than
# quietly built with another dialect.  The C++ compatibility test
# overrides this with a property of its own, because the public header
# is expected to work from an older C++ as well.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# The value chosen here names the startup file and the system-call
# trampoline of the target under assembly/, and the system-call numbers
# io/syscall_linux.cxx selects for it.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(STDOC_PROCESSOR "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(STDOC_PROCESSOR "x86_64")
else()
    message(FATAL_ERROR
        "libstdoc: unsupported target architecture \"${CMAKE_SYSTEM_PROCESSOR}\"; "
        "implemented: aarch64 and x86_64")
endif()

# Compiler selection.
#
# The flags below are GNU-style.  They are only used after the compiler
# has been identified, never assumed to exist.
if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR
        "libstdoc: C compiler \"${CMAKE_C_COMPILER_ID}\" is not "
        "supported; GNU C or Clang is required")
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR
        "libstdoc: C++ compiler \"${CMAKE_CXX_COMPILER_ID}\" is not "
        "supported; GNU C++ or Clang is required")
endif()

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

# The C++ warning set is the same baseline minus the two options that
# only exist for C, plus the warnings that keep a C++ implementation
# honest: a C-style cast, a class with virtual functions and a
# non-virtual destructor, an overload hidden by a declaration, and a
# zero written where a null pointer belongs.
set(STDOC_CXX_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wcast-qual
    -Wundef
    -Wswitch-default
    -Wdouble-promotion
    -Wvla
    -Wold-style-cast
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wzero-as-null-pointer-constant)

# The library is compiled as freestanding C++ code: it may not assume a
# host C library, may not use compiler builtins in place of its own
# functions, may not depend on the stack protector, which needs a
# runtime symbol that does not exist here, and may not use any of the
# C++ runtime facilities - exceptions, RTTI or guarded function-local
# statics - that a freestanding link cannot provide.
set(STDOC_LIBRARY_FLAGS
    -ffreestanding
    -fno-builtin
    -fno-stack-protector
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics)

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

# The implementation is C++23 and freestanding, so it may use only the
# freestanding subset of the C++ standard library.  The probe checks
# that the toolchain provides the headers the implementation includes
# and that the language facilities it relies on are available; without
# it, a toolchain with no freestanding C++ library would only fail
# later, as a wall of compiler errors inside an implementation file.
#
# libstdc++ supports freestanding mode from GCC 13 on; libc++ and the
# MSVC standard library do not implement it.  A toolchain that does not
# provide the headers fails the configuration with a message that names
# what is missing, rather than quietly falling back to a hosted build.
include(CheckCXXSourceCompiles)

# try_compile builds an executable by default, and a freestanding C++
# program has no hosted startup to link against, so the probe is
# compiled into a static library instead of being linked.
set(STDOC_SAVED_TRY_COMPILE_TARGET_TYPE "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_REQUIRED_FLAGS "${STDOC_LIBRARY_FLAGS}")

check_cxx_source_compiles("
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

constexpr std::size_t probe_alignment = alignof(std::max_align_t);
constinit std::uintptr_t probe_pointer = 0;
static_assert(INT_MAX > 0);

extern \"C\" int stdoc_probe(const char *format, ...);

extern \"C\" int stdoc_probe(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    va_end(args);

    return static_cast<int>(probe_pointer + probe_alignment) +
           ((format != nullptr) ? 1 : 0);
}
" STDOC_FREESTANDING_CXX_OK)

set(CMAKE_TRY_COMPILE_TARGET_TYPE "${STDOC_SAVED_TRY_COMPILE_TARGET_TYPE}")
unset(CMAKE_REQUIRED_FLAGS)

if(NOT STDOC_FREESTANDING_CXX_OK)
    message(FATAL_ERROR
        "libstdoc: the C++ compiler \"${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}\" does not provide the "
        "freestanding C++23 standard library the implementation needs: "
        "<climits>, <cstdarg>, <cstddef> and <cstdint> must be usable "
        "under -ffreestanding -fno-exceptions -fno-rtti "
        "(libstdc++ provides them from GCC 13 on)")
endif()

# The library.
#
# The startup file and the system-call trampoline of the target
# architecture, chosen by the guard above, are part of the archive
# because the entry point belongs to the runtime this library provides.
# The test executables still compile the startup file into themselves so
# that linking a test never depends on the linker recovering a symbol
# from an archive.
#
# mem/heap.cxx is the dynamic memory allocator: its arena is a static
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
# static assertion in mem/heap.cxx, which knows the alignment of the
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
    assembly/start_${STDOC_PROCESSOR}.S
    assembly/syscall_${STDOC_PROCESSOR}.S
    io/syscall_linux.cxx
    io/file.cxx
    io/string.cxx
    io/vfprintf.cxx
    io/printf.cxx
    io/fprintf.cxx
    mem/heap.cxx
)

set_target_properties(stdoc PROPERTIES OUTPUT_NAME stdoc)

# Consumers of the build tree use the same name they get once the
# library is installed, so a project can be pointed at either without
# being edited.
add_library(libstdoc::stdoc ALIAS stdoc)

target_compile_options(stdoc PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${STDOC_CXX_WARNINGS}>
    $<$<COMPILE_LANGUAGE:CXX>:${STDOC_LIBRARY_FLAGS}>
)

# The implementation is C++23, and asking for the standard as a compile
# feature makes CMake reject a compiler that does not support it when
# the project is configured, instead of failing somewhere inside a
# translation unit.  The requirement is PRIVATE: a program that links
# the archive does not have to be compiled as C++23 itself.
target_compile_features(stdoc PRIVATE cxx_std_23)

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
