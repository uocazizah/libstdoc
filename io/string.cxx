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
 * Minimal internal string and memory helpers.
 *
 * The library is linked with "-nostdlib", so it may not call strlen,
 * memcpy or memset from a host C library: those symbols would either
 * be missing at link time or, worse, silently resolve to a different
 * implementation than the one under test.  The helpers below are
 * therefore defined here and prefixed so that they never collide with
 * a host C library when libstdoc is linked into a hosted program.
 *
 * None of these functions is part of the public API.
 */

#include "stdoc_io.h"

size_t stdoc_strlen(const char *p_string)
{
    size_t length;

    length = 0;

    if (p_string == NULL)
    {
        return 0;
    }

    while (p_string[length] != '\0')
    {
        length = length + 1;
    }

    return length;
}

void *stdoc_memcpy(void *p_destination, const void *p_source, size_t length)
{
    unsigned char *p_to;
    const unsigned char *p_from;
    size_t index;

    if ((p_destination == NULL) || (p_source == NULL))
    {
        return p_destination;
    }

    if (length == 0)
    {
        return p_destination;
    }

    if (p_destination == p_source)
    {
        return p_destination;
    }

    p_to   = (unsigned char *)p_destination;
    p_from = (const unsigned char *)p_source;

    for (index = 0; index < length; index = index + 1)
    {
        p_to[index] = p_from[index];
    }

    return p_destination;
}

void *stdoc_memset(void *p_destination, int value, size_t length)
{
    unsigned char *p_to;
    size_t index;

    if (p_destination == NULL)
    {
        return p_destination;
    }

    p_to = (unsigned char *)p_destination;

    for (index = 0; index < length; index = index + 1)
    {
        p_to[index] = (unsigned char)value;
    }

    return p_destination;
}
