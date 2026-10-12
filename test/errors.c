/*
 * libstdoc - failure path test
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
 * The paths on which the formatted functions report failure.  Each
 * check adds to a counter that becomes the exit status, so the test
 * carries its own verdict and needs no golden file.
 *
 * The internal header is included deliberately: a stream bound to a
 * bad descriptor cannot be obtained through the public interface, and
 * the sticky-error behaviour that follows a failed write is worth
 * testing.
 */

#include "stdoc_io.h"

static int failures;

/*
 * Record a check that did not hold.
 */
static void check(int condition, const char *p_what)
{
    if (condition == 0)
    {
        failures = failures + 1;
        stdoc_fprintf(stdoc_stderr, "FAIL: %s\n", p_what);
    }
}

/*
 * A stream bound to a descriptor that does not exist.  Writing to it
 * has to fail, and the failure has to stick.
 */
static stdoc_FILE broken_stream =
{
    .fd = -1,
    .error = 0,
    .unbuffered = 1,
    .length = 0,
    .buffer = { 0 }
};

int main(void)
{
    failures = 0;

    /* A field wider than "int" is a failure, not a very wide field. */
    check(stdoc_printf("%2147483648d", 1) == -1, "width literal overflows");
    check(stdoc_printf("%99999999999d", 1) == -1, "wide width literal overflows");
    check(stdoc_printf("[%4294967296d]", 1) == -1, "huge width literal overflows");
    check(stdoc_printf("%.99999999999d", 1) == -1, "precision literal overflows");

    /* A field that cannot fit in the return value is a failure too. */
    check(stdoc_printf("%2147483647d", 1) == -1, "field exceeds return value");
    check(stdoc_printf("%2147483647s", "x") == -1, "string field too wide");

    /* Null arguments. */
    check(stdoc_printf(NULL) == -1, "null format");
    check(stdoc_fprintf(NULL, "x") == -1, "null stream");
    check(stdoc_fprintf(stdoc_stdout, NULL) == -1, "null format with stream");

    /* A write that the operating system rejects. */
    check(stdoc_fprintf(&broken_stream, "x") == -1, "write to a bad descriptor");
    check(broken_stream.error != 0, "failure is recorded in the stream");
    check(stdoc_fprintf(&broken_stream, "y") == -1,
          "a stream in error keeps failing");
    check(stdoc_stream_flush(&broken_stream) == -1,
          "flushing a stream in error fails");

    /* Nothing staged must flush cleanly, and the good streams work. */
    check(stdoc_stream_flush(stdoc_stdout) == 0, "flush with nothing staged");
    check(stdoc_printf("") == 0, "empty format");
    check(stdoc_fprintf(stdoc_stderr, "") == 0, "empty format on stderr");

    if (failures == 0)
    {
        stdoc_fprintf(stdoc_stderr, "error handling: all checks passed\n");
    }

    return failures;
}
