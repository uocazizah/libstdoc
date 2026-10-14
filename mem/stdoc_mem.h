/*
 * libstdoc - a minimal standard C library
 * Copyright (C) 2026 Uoc Azizah
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Alternatively, this program may be used, copied, modified and
 * distributed under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Alternatively, and at your option, this program may be used,
 * copied, modified and distributed under the terms of the Apache
 * License, Version 2.0, in which case the provisions of that
 * license apply instead of those above.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with this program;
 * if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef STDOC_MEM_H
#define STDOC_MEM_H

/*
 * Internal interface of the libstdoc heap (mem/heap.c).
 *
 * Nothing declared here is part of the public API.  The public
 * allocation functions live in "<libstdoc.h>"; this header only
 * carries the sizing knob and the accounting that the tests use to
 * prove that no allocation is left behind.
 */

#include <stddef.h>

/*
 * Size of the heap arena, in bytes.
 *
 * The arena is a static object, so the size is fixed when the library
 * is compiled: the heap never grows and a program can never leak more
 * than this many bytes into it.  The default is deliberately small -
 * the only library code that allocates is the formatting engine, which
 * needs less than a hundred bytes at a time - and a build may override
 * it with "-DSTDOC_HEAP_SIZE=<bytes>" or through the CMake cache
 * variable of the same name.
 */
#ifndef STDOC_HEAP_SIZE
#define STDOC_HEAP_SIZE 4096
#endif

/*
 * Number of payload bytes currently reserved, and number of blocks
 * they are reserved in.  Both are zero once every allocation has been
 * released, which is exactly what the tests assert after exercising
 * the library.
 */
size_t stdoc_heap_live_bytes(void);
size_t stdoc_heap_live_blocks(void);

/*
 * Total usable size of the arena, in bytes.  The accounting above is
 * compared against this value by tests that exhaust the heap.
 */
size_t stdoc_heap_capacity(void);

#endif /* STDOC_MEM_H */
