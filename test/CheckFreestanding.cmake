# Checks that the built archive is really freestanding.
#
# The archive is built with "-ffreestanding" and a static library is
# never linked on its own, so a reference to the C++ runtime or to a
# host C library inside one of its members would only show up when a
# program links the archive - and even then only if the linker happens
# to pull that member in.  This script inspects the archive directly,
# which is the only way to prove the property for every member.
#
# What is verified:
#
#   1. Every undefined symbol is either the program's "main" or a
#      "stdoc"-prefixed symbol of the library itself.  Anything else -
#      "__cxa_*", "_Unwind_*", "operator new", "memset", "memcpy", a
#      compiler runtime helper such as "__udivti3" - is a dependence on
#      a runtime the archive must not have.
#
#   2. No exported symbol carries a C++ mangled name ("_Z..."): the
#      archive exports the C interface and nothing else.  Internal
#      helpers are allowed to be mangled, because they have internal
#      linkage and are not visible to a linker.
#
#   3. No member has a section that exists only to run code before or
#      after the program: ".init_array", ".fini_array", ".ctors",
#      ".dtors", ".preinit_array", ".tdata" or ".tbss".  The
#      freestanding entry point walks no initialiser array, so a
#      dynamic initialiser or a thread-local object would never run.
#
# Invoked by CTest with -DARCHIVE=..., -DNM=... and -DREADELF=....
# The build fails the test rather than skipping it when nm or readelf
# is unavailable, so the property is never assumed.

if(NOT EXISTS "${ARCHIVE}")
    message(FATAL_ERROR
        "archive-check: \"${ARCHIVE}\" does not exist")
endif()

if(NOT NM)
    message(FATAL_ERROR
        "archive-check: nm was not found; the freestanding check "
        "cannot be run")
endif()

# Undefined symbols.
#
# nm -P prints one symbol per line as "name U", after a "member(object)"
# header line for each archive member, which is used below to name the
# member in the diagnostic.

execute_process(
    COMMAND "${NM}" --format=posix --extern-only --undefined-only
            "${ARCHIVE}"
    OUTPUT_VARIABLE undefined_output
    ERROR_VARIABLE nm_error
    RESULT_VARIABLE nm_result)

if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR
        "archive-check: nm failed on \"${ARCHIVE}\": ${nm_error}")
endif()

set(member "")
set(offenders "")

string(REPLACE "\n" ";" undefined_lines "${undefined_output}")

foreach(line IN LISTS undefined_lines)
    string(STRIP "${line}" stripped)

    if(stripped MATCHES "^(.+):$")
        set(member "${CMAKE_MATCH_1}")
        continue()
    endif()

    if(NOT stripped MATCHES "^(.+) U$")
        continue()
    endif()

    set(name "${CMAKE_MATCH_1}")

    if(name MATCHES "^stdoc_")
        continue()
    endif()

    if(name STREQUAL "main")
        continue()
    endif()

    list(APPEND offenders "${name} (from ${member})")
endforeach()

if(offenders)
    list(JOIN offenders "\n  " offender_text)

    message(FATAL_ERROR
        "archive-check: \"${ARCHIVE}\" refers to symbols outside "
        "libstdoc:\n  ${offender_text}\n"
        "The library must not need a C library, a C++ runtime or a "
        "compiler runtime helper.")
endif()

# Exported symbols.
#
# The archive may not export a mangled C++ name, and it may not export
# anything a link against the installed headers did not promise.  Only
# the C interface declared in the public header is checked here; the
# internal helpers live in an anonymous namespace and are local.

execute_process(
    COMMAND "${NM}" --format=posix --extern-only --defined-only
            "${ARCHIVE}"
    OUTPUT_VARIABLE defined_output
    ERROR_VARIABLE nm_error
    RESULT_VARIABLE nm_result)

if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR
        "archive-check: nm failed on \"${ARCHIVE}\": ${nm_error}")
endif()

set(member "")
set(offenders "")

string(REPLACE "\n" ";" defined_lines "${defined_output}")

foreach(line IN LISTS defined_lines)
    string(STRIP "${line}" stripped)

    if(stripped MATCHES "^(.+):$")
        set(member "${CMAKE_MATCH_1}")
        continue()
    endif()

    if(NOT stripped MATCHES "^([^ ]+) ([TDBRWVC]) [0-9a-fA-F]+( [0-9a-fA-F]+)?$")
        continue()
    endif()

    set(name "${CMAKE_MATCH_1}")

    if(name MATCHES "^_Z")
        list(APPEND offenders "${name} (from ${member})")
    endif()
endforeach()

if(offenders)
    list(JOIN offenders "\n  " offender_text)

    message(FATAL_ERROR
        "archive-check: \"${ARCHIVE}\" exports mangled C++ names:\n  "
        "${offender_text}\n"
        "Every exported symbol must be part of the C interface.")
endif()

# Sections that exist only to run code outside the program.
#
# A dynamic initialiser would need ".init_array" (or its older ".ctors"
# spelling), a thread-local object needs ".tdata"/".tbss" and the
# freestanding entry point runs none of them, so their presence is
# reported even when the symbols around them look harmless.

if(NOT READELF)
    message(FATAL_ERROR
        "archive-check: readelf was not found; the section check "
        "cannot be run")
endif()

execute_process(
    COMMAND "${READELF}" --section-headers --wide "${ARCHIVE}"
    OUTPUT_VARIABLE sections_output
    ERROR_VARIABLE readelf_error
    RESULT_VARIABLE readelf_result)

if(NOT readelf_result EQUAL 0)
    message(FATAL_ERROR
        "archive-check: readelf failed on \"${ARCHIVE}\": ${readelf_error}")
endif()

foreach(section .init_array .fini_array .preinit_array .ctors .dtors
                .tdata .tbss)
    string(FIND "${sections_output}" "${section}" position)

    if(NOT position EQUAL -1)
        message(FATAL_ERROR
            "archive-check: \"${ARCHIVE}\" has a \"${section}\" section; "
            "the freestanding entry point would never run it")
    endif()
endforeach()

message(STATUS
    "archive-check: \"${ARCHIVE}\" is freestanding")
