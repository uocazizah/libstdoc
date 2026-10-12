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

/*
 * Operating-system system-call wrappers.
 *
 * This file is the only place where Linux system-call numbers and the
 * "-errno in the return register" convention appear.  Everything above
 * it works in terms of 0 / -1 results, so the rest of the library does
 * not depend on the operating system.
 *
 * Target: Linux on aarch64 (the generic Linux system-call ABI, where
 * the number is carried in x8, arguments in x0..x5 and the result in
 * x0).  Other operating systems and architectures must provide their
 * own file and are rejected at configuration time rather than being
 * silently mapped onto these numbers.
 */

#include <stdint.h>

#include "stdoc_io.h"

/*
 * Linux system-call numbers for aarch64.
 *
 * They are numeric on purpose: no "<asm/unistd.h>" or other kernel
 * header is consulted, so the build has no dependency outside this
 * repository.
 */
#define STDOC_SYS_CLOSE  57L
#define STDOC_SYS_READ   63L
#define STDOC_SYS_WRITE  64L
#define STDOC_SYS_EXIT_GROUP 94L

/*
 * Largest magnitude a kernel error can have.  The kernel returns
 * "-errno" in the range -4095 .. -1 on failure and a non-negative
 * value on success, so a single unsigned comparison classifies both.
 */
#define STDOC_KERNEL_ERROR_LIMIT 4096L

/*
 * Translate a raw kernel result into the 0 / -1 convention.
 *
 * The comparison is done on the unsigned representation so that it is
 * well defined for every long value, including negative ones.
 */
static int stdoc_kernel_result(long result)
{
    unsigned long magnitude;

    magnitude = (unsigned long)result;

    if (magnitude >= (unsigned long)STDOC_KERNEL_ERROR_LIMIT)
    {
        return -1;
    }

    return 0;
}

int stdoc_sys_write(int fd, const void *p_buffer, size_t length)
{
    long result;

    if ((p_buffer == NULL) && (length != 0))
    {
        return -1;
    }

    result = stdoc_syscall(STDOC_SYS_WRITE, (long)fd,
                           (long)(uintptr_t)p_buffer, (long)length,
                           0L, 0L, 0L);

    return stdoc_kernel_result(result);
}

int stdoc_sys_read(int fd, void *p_buffer, size_t length)
{
    long result;

    if ((p_buffer == NULL) && (length != 0))
    {
        return -1;
    }

    result = stdoc_syscall(STDOC_SYS_READ, (long)fd,
                           (long)(uintptr_t)p_buffer, (long)length,
                           0L, 0L, 0L);

    return stdoc_kernel_result(result);
}

int stdoc_sys_close(int fd)
{
    long result;

    result = stdoc_syscall(STDOC_SYS_CLOSE, (long)fd, 0L, 0L, 0L, 0L, 0L);

    return stdoc_kernel_result(result);
}

void stdoc_sys_exit(int status)
{
    /*
     * exit_group is used rather than exit so that every thread of the
     * process is terminated; libstdoc does not create threads, but a
     * program linked against it may.
     */
    (void)stdoc_syscall(STDOC_SYS_EXIT_GROUP, (long)status, 0L, 0L,
                        0L, 0L, 0L);

    /*
     * The system call never returns.  The loop below only satisfies
     * the compiler that this function does not fall through, and
     * guards against a kernel that somehow resumes execution.
     */
    for (;;)
    {
        /* spin */
    }
}
