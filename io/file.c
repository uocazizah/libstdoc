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
 * The standard streams and their staging buffer.
 *
 * Every stream owns a fixed-size buffer embedded in the object, so the
 * library performs no allocation at all.  Bytes are staged there and
 * handed to the operating system in a single write once the buffer is
 * full, once a newline is staged, or when the caller finishes a
 * complete formatted call.  Flushing eagerly on those three events
 * keeps output ordered without any exit-time hook, which the
 * freestanding startup path does not provide.
 */

#include "stdoc_io.h"

/*
 * The three standard streams.  The pointers are const because a caller
 * may not rebind stdout to another object; the streams themselves are
 * ordinary mutable objects.
 *
 * stderr is marked unbuffered so that it is never reordered with
 * respect to stdout: every write is pushed to the kernel as soon as it
 * is staged.  stdout and stdin keep a staging buffer.
 */
static stdoc_FILE stdoc_stream_stdin =
{
    0,   /* fd          */
    0,   /* error       */
    1,   /* unbuffered  */
    0,   /* length      */
    { 0 }
};

static stdoc_FILE stdoc_stream_stdout =
{
    1,   /* fd          */
    0,   /* error       */
    0,   /* buffered    */
    0,   /* length      */
    { 0 }
};

static stdoc_FILE stdoc_stream_stderr =
{
    2,   /* fd          */
    0,   /* error       */
    1,   /* unbuffered  */
    0,   /* length      */
    { 0 }
};

stdoc_FILE *const stdoc_stdin  = &stdoc_stream_stdin;
stdoc_FILE *const stdoc_stdout = &stdoc_stream_stdout;
stdoc_FILE *const stdoc_stderr = &stdoc_stream_stderr;

int stdoc_stream_flush(stdoc_FILE *stream)
{
    size_t length;

    if (stream == NULL)
    {
        return -1;
    }

    if (stream->error != 0)
    {
        return -1;
    }

    length = stream->length;

    if (length == 0)
    {
        return 0;
    }

    if (stdoc_sys_write(stream->fd, stream->buffer, length) != 0)
    {
        stream->error = 1;
        stream->length = 0;
        return -1;
    }

    stream->length = 0;
    return 0;
}

void stdoc_stream_discard(stdoc_FILE *stream)
{
    if (stream == NULL)
    {
        return;
    }

    stream->length = 0;
}

/*
 * Stage bytes into the stream buffer, flushing whenever it fills or a
 * newline is queued.  Returns 0 on success, -1 on failure.
 *
 * The loop is written so that a payload larger than the buffer is
 * handled by repeated flushes rather than by an oversized stack copy.
 */
int stdoc_stream_write(stdoc_FILE *stream, const void *p_buffer, size_t length)
{
    const unsigned char *p_bytes;
    size_t index;
    size_t available;

    if (stream == NULL)
    {
        return -1;
    }

    if ((p_buffer == NULL) && (length != 0))
    {
        stream->error = 1;
        return -1;
    }

    if (stream->error != 0)
    {
        return -1;
    }

    p_bytes = (const unsigned char *)p_buffer;

    for (index = 0; index < length; index = index + 1)
    {
        available = STDOC_BUFFER_SIZE - stream->length;

        if (available == 0)
        {
            if (stdoc_stream_flush(stream) != 0)
            {
                return -1;
            }
            available = STDOC_BUFFER_SIZE;
        }

        stream->buffer[stream->length] = p_bytes[index];
        stream->length = stream->length + 1;

        if (p_bytes[index] == '\n')
        {
            if (stdoc_stream_flush(stream) != 0)
            {
                return -1;
            }
        }
    }

    /*
     * An unbuffered stream hands every write to the kernel straight
     * away, so any bytes still staged once the caller is done are
     * pushed out here.  On a buffered stream this is a no-op and the
     * caller decides when to flush.
     */
    if (stream->unbuffered != 0)
    {
        if (stdoc_stream_flush(stream) != 0)
        {
            return -1;
        }
    }

    return 0;
}
