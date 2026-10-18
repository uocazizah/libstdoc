/*
 * libstdoc - heap and leak test
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

/*
 * The heap, exercised with small allocations only.
 *
 * The arena is 4096 bytes by default, so anything this test leaks
 * stays a few kilobytes at most, and the accounting in stdoc_mem.hxx
 * turns any leak into a failing check: after every scenario the live
 * byte and block counts must be back at zero.  The exhaustion scenario
 * deliberately fills the arena to prove that a full heap behaves -
 * stdoc_malloc reports it, printf falls back to its stack buffer, and
 * releasing everything restores the whole arena.
 *
 * Each check adds to a counter that becomes the exit status, so the
 * test carries its own verdict and needs no golden file.
 */

#include <stddef.h>
#include <stdint.h>

#include "stdoc_io.hxx"
#include "stdoc_mem.hxx"

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
 * A successful allocation is aligned, counted and released cleanly;
 * a request for nothing is not a request at all.
 */
static void test_allocation_and_release(void)
{
    void  *p_zero;
    void  *p_small;

    p_zero = stdoc_malloc(0);
    check(p_zero == NULL, "malloc of zero bytes returns NULL");

    p_small = stdoc_malloc(1);
    check(p_small != NULL, "malloc of one byte returns a pointer");
    check(stdoc_heap_live_blocks() == 1, "a live block is counted");
    check(stdoc_heap_live_bytes() > 0, "live bytes are counted");

    if (p_small != NULL)
    {
        check((((uintptr_t)p_small) % _Alignof(max_align_t)) == 0,
              "a payload is aligned for any object");
    }

    stdoc_free(p_small);
    check(stdoc_heap_live_blocks() == 0, "release drops the block count");
    check(stdoc_heap_live_bytes() == 0, "release drops the byte count");
}

/*
 * Blocks that are allocated together keep their own contents.
 *
 * How many blocks fit in the arena is the allocator's own business -
 * the test does not know how large a block header is - so instead of
 * computing the answer it asks the heap: it takes as many blocks as it
 * can and keeps going with at least two of them, which is the least a
 * scenario that compares neighbours needs.  The payload is halved when
 * the arena is too small to hold two blocks of that size.
 */
static size_t take_blocks(unsigned char *p_blocks[], size_t wanted,
                          size_t payload)
{
    size_t count;

    for (count = 0; count < wanted; count = count + 1)
    {
        p_blocks[count] = (unsigned char *)stdoc_malloc(payload);

        if (p_blocks[count] == NULL)
        {
            break;
        }
    }

    return count;
}

static void release_blocks(unsigned char *p_blocks[], size_t count)
{
    size_t index;

    for (index = 0; index < count; index = index + 1)
    {
        stdoc_free(p_blocks[index]);
        p_blocks[index] = NULL;
    }
}

static void test_blocks_do_not_overlap(void)
{
    unsigned char *p_blocks[4];
    size_t         payload;
    size_t         count;
    size_t         index;
    size_t         byte;
    int            intact;

    payload = 32;

    for (;;)
    {
        count = take_blocks(p_blocks, 4, payload);

        if (count >= 2)
        {
            break;
        }

        release_blocks(p_blocks, count);

        if (payload <= 16)
        {
            check(0, "the heap serves at least two blocks");
            return;
        }

        payload = payload / 2;
    }

    check(count > 0, "the heap serves allocations");

    for (index = 0; index < count; index = index + 1)
    {
        (void)stdoc_memset(p_blocks[index], (int)(0xA0u + index), payload);
    }

    intact = 1;
    for (index = 0; index < count; index = index + 1)
    {
        for (byte = 0; byte < payload; byte = byte + 1)
        {
            if (p_blocks[index][byte] != (unsigned char)(0xA0u + index))
            {
                intact = 0;
            }
        }
    }

    check(intact != 0, "neighbouring blocks do not overlap");

    release_blocks(p_blocks, count);

    check(stdoc_heap_live_bytes() == 0, "every block was released");
}

/*
 * Pointers that are not the start of a live block are ignored by
 * stdoc_free: the heap is left exactly as it was, including when the
 * same block is released twice.
 */
static void test_invalid_free_is_ignored(void)
{
    void  *p_block;
    size_t before;

    p_block = stdoc_malloc(16);
    check(p_block != NULL, "a block for the invalid frees is available");

    if (p_block == NULL)
    {
        return;
    }

    before = stdoc_heap_live_bytes();

    stdoc_free((char *)p_block + 1);
    stdoc_free((char *)p_block + 16);
    stdoc_free((void *)(uintptr_t)0x1234);

    check(stdoc_heap_live_bytes() == before,
          "an invalid free leaves the heap unchanged");

    stdoc_free(p_block);
    check(stdoc_heap_live_bytes() == 0, "the valid free still works");

    stdoc_free(p_block);
    check(stdoc_heap_live_bytes() == 0, "a double free changes nothing");
    stdoc_free(NULL);
    check(stdoc_heap_live_bytes() == 0, "freeing NULL changes nothing");
}

/*
 * Fill the arena, prove printf still works without it, then release
 * everything: the counters return to zero and one block covering
 * three quarters of the arena proves the free blocks were merged
 * again rather than left as fragments.
 */
static void test_exhaustion_and_release(void)
{
    void  *p_head;
    void  *p_block;
    void  *p_large;
    size_t payload;
    size_t count;
    int    written;

    /*
     * The payload is derived from the arena - a sixteenth of it, and
     * never smaller than the smallest request worth making - so that
     * the scenario fills whichever heap the library was built with
     * instead of assuming room for 64 byte blocks.  The previous head
     * is remembered in the payload itself, so the list of blocks costs
     * no storage of its own and works for any arena size.
     */
    payload = stdoc_heap_capacity() / 16u;

    if (payload < 16u)
    {
        payload = 16u;
    }

    p_head = NULL;
    count = 0;

    for (;;)
    {
        p_block = stdoc_malloc(payload);

        if (p_block == NULL)
        {
            break;
        }

        *(void **)p_block = p_head;
        p_head = p_block;
        count = count + 1;
    }

    check(count > 0, "the heap serves allocations");
    check(stdoc_malloc(payload) == NULL, "an exhausted heap reports NULL");

    written = stdoc_printf("heap exhausted, falling back: %d\n", 42);
    check(written > 0,
          "printf still works when the heap has nothing left");

    while (p_head != NULL)
    {
        p_block = p_head;
        p_head = *(void **)p_head;
        stdoc_free(p_block);
    }

    check(stdoc_heap_live_blocks() == 0, "every block was released");
    check(stdoc_heap_live_bytes() == 0, "every byte was released");

    p_large = stdoc_malloc(stdoc_heap_capacity() - (stdoc_heap_capacity() / 4u));
    check(p_large != NULL, "free blocks are merged back into one");
    stdoc_free(p_large);
    check(stdoc_heap_live_bytes() == 0, "the merged block was released");
}

/*
 * The formatted functions borrow scratch memory and must give it
 * back, whatever they are asked to print.
 */
static void test_printf_releases_its_scratch(void)
{
    int written;

    written = 0;
    written = written + stdoc_printf("[%d][%i][%u]\n", -42, 42, 42u);
    written = written + stdoc_printf("[%x][%X][%o][%c]\n", 255u, 255u, 8u, 'Z');
    written = written + stdoc_printf("[%10.4d][%-8u][%s]\n", 1234, 99u, "text");
    written = written + stdoc_printf("[%20s][%.3s][%%]\n", "wide", "abcdef");
    written = written + stdoc_printf("[%20d][%-20d][%08d][%.4d]\n", 7, 7, 7, 7);
    written = written + stdoc_printf("[%p][%s]\n", (void *)0, (const char *)NULL);

    check(written > 0, "the formatted calls produced output");
    check(stdoc_heap_live_blocks() == 0,
          "printf left no block behind");
    check(stdoc_heap_live_bytes() == 0,
          "printf left no byte behind");
}

/*
 * The safe entry point: checked size arithmetic, zero-filled memory
 * and a release that reports nothing because the pointer is valid.
 */
static void test_safe_allocation(void)
{
    unsigned char *p_block;
    size_t         count;
    size_t         total;
    size_t         index;
    int            all_zero;
    void          *p_nothing;

    /*
     * The request is sized from the arena so that it fits whichever
     * heap the library was built with: a quarter of the arena in
     * blocks of 16 bytes, capped at the 64 bytes this scenario has
     * always asked for.
     */
    total = stdoc_heap_capacity() / 4u;
    count = total / 16u;

    if (count == 0)
    {
        count = 1;
        total = 16u;
    }

    if (total > 64u)
    {
        total = 64u;
        count = 4u;
    }

    p_block = stdoc_malloc_safe(count, 16);
    check(p_block != NULL, "malloc_safe returns a pointer");
    check(stdoc_heap_live_bytes() == total,
          "malloc_safe reserves the whole request");

    all_zero = 1;
    if (p_block != NULL)
    {
        for (index = 0; index < total; index = index + 1)
        {
            if (p_block[index] != 0u)
            {
                all_zero = 0;
            }
        }
    }
    check(all_zero != 0, "malloc_safe returns zeroed memory");

    p_nothing = stdoc_malloc_safe(0, 16);
    check(p_nothing == NULL, "a request for nothing allocates nothing");

    stdoc_free_safe(p_block);
    check(stdoc_heap_live_bytes() == 0, "free_safe released the block");
    stdoc_free_safe(NULL);
    check(stdoc_heap_live_bytes() == 0, "free_safe ignores NULL");
}

int main(void)
{
    failures = 0;

    test_allocation_and_release();
    test_blocks_do_not_overlap();
    test_invalid_free_is_ignored();
    test_exhaustion_and_release();
    test_printf_releases_its_scratch();
    test_safe_allocation();

    if (failures == 0)
    {
        stdoc_fprintf(stdoc_stderr, "heap: all checks passed\n");
    }

    return failures;
}
