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

#ifndef STDOC_IO_H
#define STDOC_IO_H

/*
 * Internal interface shared by the libstdoc I/O and platform layers.
 *
 * Nothing declared here is part of the public API.  The declarations
 * exist so that the formatting engine, the stream objects and the
 * operating-system wrappers stay separated and individually testable.
 */

#include <stddef.h>

#include "libstdoc.h"

/*
 * Size of the per-stream staging buffer, in bytes.
 *
 * The buffer lives inside the stream object, so no allocation is ever
 * performed by libstdoc.  256 bytes keeps a whole formatted line in
 * one write system call in the common case without growing the
 * stream object meaningfully.
 */
#define STDOC_BUFFER_SIZE 256

/*
 * A libstdoc output stream.
 *
 * "fd" is an operating-system file descriptor.  "error" is a sticky
 * failure flag: once set, every further write to the stream fails
 * without touching the system call layer.  "unbuffered" forces the
 * staged bytes out after each write instead of waiting for the buffer
 * to fill, which is how stderr keeps its promised ordering.  "length"
 * is the number of bytes currently staged in "buffer".
 */
struct stdoc_FILE
{
    int    fd;
    int    error;
    int    unbuffered;
    size_t length;
    char   buffer[STDOC_BUFFER_SIZE];
};

/*
 * Operating-system layer (io/syscall_linux.c).
 *
 * Each wrapper returns 0 on success and -1 on failure, mirroring the
 * convention documented for the public functions.  The wrappers are
 * the only place that knows the system-call numbers and the argument
 * convention of the target, so the formatting engine stays portable.
 */
int stdoc_sys_write(int fd, const void *p_buffer, size_t length);
int stdoc_sys_read(int fd, void *p_buffer, size_t length);
int stdoc_sys_close(int fd);

/* Terminates the process; never returns.  "status" is the exit code. */
void stdoc_sys_exit(int status) __attribute__((noreturn));

/*
 * Generic system-call trampoline (assembly/syscall.S).
 *
 * Arguments are passed following the ordinary C calling convention of
 * the target; the trampoline rearranges them into the system-call
 * convention of the kernel.  The raw kernel return value is returned
 * unmodified, so callers must interpret negative values in the
 * "-4095 .. -1" range as errors.
 */
long stdoc_syscall(long number, long arg1, long arg2, long arg3,
                   long arg4, long arg5, long arg6);

/*
 * Stream helpers (io/file.c).
 *
 * stdoc_stream_write stages "length" bytes into the stream buffer and
 * flushes it when full, when a newline is staged, or when the buffer
 * would otherwise be left dirty.  It returns 0 on success and -1 on
 * failure, and records the failure in the stream.
 *
 * stdoc_stream_flush pushes the staged bytes to the operating system
 * and clears the buffer.  It succeeds when nothing is staged.
 *
 * stdoc_stream_discard throws away the staged bytes without writing
 * them, which is what a failed call has to do so that half of a
 * conversion never reaches the output.
 */
int stdoc_stream_write(stdoc_FILE *stream, const void *p_buffer, size_t length);
int stdoc_stream_flush(stdoc_FILE *stream);
void stdoc_stream_discard(stdoc_FILE *stream);

/*
 * Formatting engine (io/vfprintf.c).
 *
 * Returns the number of characters written, or -1 on failure.
 */
int stdoc_vfprintf(stdoc_FILE *stream, const char *format, va_list args);

/*
 * Internal string helpers.
 *
 * These exist so that the library links with "-nostdlib" without
 * pulling symbols out of a host C library.  They are deliberately
 * prefixed and are not exported as public API.
 */
size_t stdoc_strlen(const char *p_string);
void  *stdoc_memcpy(void *p_destination, const void *p_source, size_t length);
void  *stdoc_memset(void *p_destination, int value, size_t length);

#endif /* STDOC_IO_H */
