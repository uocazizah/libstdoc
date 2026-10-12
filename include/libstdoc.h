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
 * freestanding builds: it depends only on "<stdarg.h>", which is
 * provided by the compiler rather than by any C library.
 */

#include <stdarg.h>

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
 */
int stdoc_printf(const char *format, ...);

/*
 * Same as stdoc_printf, but writes to "stream" instead of
 * stdoc_stdout.  See stdoc_printf for the supported conversions and
 * for the meaning of the return value.
 */
int stdoc_fprintf(stdoc_FILE *stream, const char *format, ...);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBSTDOC_H */
