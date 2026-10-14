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
 * license apply instead of those above.  You may obtain a copy of
 * the License at <https://www.apache.org/licenses/LICENSE-2.0>.
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

#ifndef LIBSTDOC_H
#define LIBSTDOC_H

/*
 * Public interface of libstdoc.
 *
 * The header is written in ISO C17 and is deliberately usable in
 * freestanding builds: it depends only on "<stdarg.h>" and
 * "<stddef.h>", which are provided by the compiler rather than by any
 * C library.
 */

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque output stream.
 *
 * The layout is an implementation detail and must not be relied upon.
 * A stream is obtained from libstdoc (stdoc_stdout, stdoc_stderr and
 * stdoc_stdin are the only streams provided in this release) and is
 * passed to the functions below.  Passing a null stream pointer, or a
 * pointer to an object that is not a libstdoc stream, is undefined
 * behaviour.
 */
typedef struct stdoc_FILE stdoc_FILE;

/*
 * Standard streams.
 *
 * stdoc_stdout is buffered; the buffer is flushed when a newline is
 * written and at the end of every completed call.  stdoc_stderr is
 * unbuffered.  stdoc_stdin is reserved for a future input release and
 * is not used by the functions declared here.
 *
 * The objects are constant pointers: the stream they point to stays
 * valid for the whole program lifetime, but the stream itself is not
 * modified through the pointer.
 */
extern stdoc_FILE *const stdoc_stdout;
extern stdoc_FILE *const stdoc_stderr;
extern stdoc_FILE *const stdoc_stdin;

/*
 * Write the formatted string described by "format" to stdoc_stdout.
 *
 * "format" must not be null.  The conversions supported in this
 * release are:
 *
 *   %d %i          signed decimal int
 *   %u             unsigned decimal int
 *   %o             unsigned octal int
 *   %x %X          unsigned hexadecimal int (lower/upper case)
 *   %c             int converted to unsigned char
 *   %s             null-terminated string
 *   %p             pointer (a null pointer prints as "(nil)")
 *   %%             a literal percent sign
 *
 * with the flags '-', '+', ' ' (space), '#' and '0'; an all-numeric
 * or '*' width; a '.' precision (numeric or '*'); and the length
 * modifiers 'hh', 'h', 'l', 'll', 'z', 't' and 'j'.
 *
 * A width given as a negative '*' argument is treated as the '-'
 * flag with a positive width.  A precision given as a negative '*'
 * argument is treated as if no precision were given.  A null "s"
 * conversion argument prints as "(null)", or as nothing at all when
 * the precision is narrower than that word.
 *
 * A width or precision written as a literal that does not fit in an
 * "int" makes the whole call fail, as does a field that would not fit
 * in the return value.
 *
 * Returns the number of characters written, or -1 if the stream
 * could not be written.  On failure the stream keeps a sticky error
 * and later calls fail immediately.
 *
 * Each call borrows one small scratch block from the libstdoc heap
 * for the duration of the call and releases it before returning, so
 * a completed call leaves nothing allocated.  When the heap is
 * exhausted the call falls back to an internal stack buffer instead,
 * so running out of heap never makes the formatted functions fail.
 */
int stdoc_printf(const char *format, ...);

/*
 * Same as stdoc_printf, but writes to "stream" instead of
 * stdoc_stdout.  See stdoc_printf for the supported conversions and
 * for the meaning of the return value.
 */
int stdoc_fprintf(stdoc_FILE *stream, const char *format, ...);

/*
 * Dynamic memory.
 *
 * libstdoc allocates from a private static arena whose size is fixed
 * when the library is compiled; the heap never grows through the
 * operating system.  A build may set the arena size with
 * "-DSTDOC_HEAP_SIZE=<bytes>" (4096 by default), which also bounds
 * the worst case leak: memory that is never released stays inside
 * that arena and is reported by the library's own accounting rather
 * than accumulating unnoticed.
 *
 * The heap is not thread-safe: libstdoc creates no threads and
 * provides no synchronisation.
 */

/*
 * Allocate "size" bytes and return a pointer suitably aligned for any
 * object of that size, or null when no block of that size is free.
 * The memory is not initialised.
 *
 * A "size" of 0 allocates nothing and returns a null pointer, which
 * is not a failure.  Returns null on failure.
 */
void *stdoc_malloc(size_t size);

/*
 * Release a block obtained from stdoc_malloc or stdoc_malloc_safe.
 *
 * A null pointer releases nothing.  Anything else that is not the
 * start of a live block - a pointer from outside the heap, a pointer
 * into the middle of a block, a block that is already free - is
 * ignored without modifying the heap.  Use stdoc_free_safe when such
 * a pointer must be reported instead.
 */
void stdoc_free(void *p);

/*
 * Same as stdoc_malloc, but with the checks a caller should not have
 * to repeat:
 *
 *   - "count * size" is computed with an overflow check, so a request
 *     whose size wraps around is rejected instead of being truncated;
 *   - the memory is filled with zero bytes before it is returned.
 *
 * A failure to satisfy the request - an overflowing size or an
 * exhausted heap - writes a diagnostic to stdoc_stderr and terminates
 * the process with status 134, so this function never returns a null
 * pointer for a request it could not meet.  The only null result is
 * the documented "nothing was requested" case ("count" or "size" is
 * 0), which allocates nothing and is not an error.
 */
void *stdoc_malloc_safe(size_t count, size_t size);

/*
 * Same as stdoc_free, but a pointer that is not the start of a live
 * block is reported: the function writes a diagnostic naming the
 * problem (outside the heap, not a block start, double free or a
 * corrupted header) to stdoc_stderr and terminates the process with
 * status 134 instead of ignoring it.  A null pointer still releases
 * nothing and is not an error.
 */
void stdoc_free_safe(void *p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBSTDOC_H */
