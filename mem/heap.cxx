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

/*
 * The libstdoc dynamic memory allocator.
 *
 * The heap is a static arena of STDOC_HEAP_SIZE bytes.  It needs no
 * system call, no operating system and no C library, which is what
 * lets this allocator live in a freestanding build, and it makes the
 * worst case leak bounded: a program that loses every pointer it owns
 * can lose at most STDOC_HEAP_SIZE bytes, and the accounting below
 * still reports them.
 *
 * Layout.  The arena is one contiguous run of blocks.  Every block
 * starts with a header - a payload size, a state and a magic value -
 * followed by its payload.  The payload size is always a multiple of
 * the fundamental alignment, and the header is rounded up to that
 * alignment too, so a payload returned by stdoc_malloc is suitably
 * aligned for any object.  Allocation scans the run for the first
 * free block that fits and splits it when the leftover is large
 * enough to hold another block; release marks the block free and
 * merges it with its free neighbours, so the arena returns to a
 * single block after everything is released.
 *
 * Invariant.  No two adjacent blocks are free.  Every operation that
 * changes the list keeps that true: a split leaves the tail free next
 * to a block that was in use, and a release absorbs the free blocks on
 * both sides of the one being released.  A release therefore has at
 * most one free neighbour on each side and can merge in the same pass
 * that found the block, instead of walking the arena a second time.
 *
 * Validation.  stdoc_free and stdoc_free_safe classify a pointer
 * before touching anything: outside the arena or misaligned, not the
 * start of a block, already free (a double free), or a block whose
 * header does not add up.  The walk that confirms "start of a block"
 * is what rejects an interior pointer: only offsets the block list
 * actually steps over are accepted.
 *
 * Strict aliasing.  The arena is a declared "unsigned char" array, so
 * inside this file it is only ever read and written through character
 * lvalues; the block headers are moved in and out with stdoc_memcpy
 * rather than by casting the storage to a structure pointer.  What a
 * caller does with the payload it receives is the caller's own
 * effective type, exactly as with any other allocator.
 *
 * Not thread-safe: libstdoc creates no threads and provides no
 * synchronisation.
 */

#include <stddef.h>
#include <stdint.h>

#include "stdoc_io.h"
#include "stdoc_mem.h"

/*
 * Payloads and headers are multiples of this alignment, which is the
 * strictest one the C library of the target can require.
 */
#define STDOC_HEAP_ALIGNMENT _Alignof(max_align_t)

_Static_assert(STDOC_HEAP_SIZE >= (4u * STDOC_HEAP_ALIGNMENT),
               "STDOC_HEAP_SIZE is too small to hold a single heap block");

/*
 * Header state and magic.
 *
 * The magic marks bytes as a header this allocator wrote; the state
 * tells a live block from a free one, which is how a double free is
 * told apart from a pointer that was never allocated.
 */
#define STDOC_HEAP_MAGIC      0x53444F43u /* "SDOC" */
#define STDOC_HEAP_STATE_FREE 0u
#define STDOC_HEAP_STATE_USED 1u

/*
 * One block header.
 *
 * "payload" is the number of usable bytes after the header, always a
 * multiple of STDOC_HEAP_ALIGNMENT.  sizeof of this structure is
 * rounded up to that alignment before it is used as an offset, so the
 * field layout may change without breaking the alignment of a
 * payload.
 */
typedef struct stdoc_heap_block
{
    size_t   payload;
    uint32_t state;
    uint32_t magic;
} stdoc_heap_block;

/*
 * Exit status of the fail-fast paths of the safe functions.  134 is
 * the conventional status of an aborted process (128 + SIGABRT), so a
 * caller that watches the status of a child sees a familiar number.
 */
#define STDOC_HEAP_FATAL_STATUS 134

/*
 * Classification of a pointer handed to stdoc_heap_release.
 */
#define STDOC_HEAP_ACCEPT   0
#define STDOC_HEAP_FOREIGN  1
#define STDOC_HEAP_INTERIOR 2
#define STDOC_HEAP_DOUBLE   3
#define STDOC_HEAP_CORRUPT  4

/*
 * Result of the "previous block" lookup when the block being looked
 * for is the first one in the arena.  A real block start is always
 * smaller than the arena, so this value cannot be mistaken for one.
 */
#define STDOC_HEAP_NO_PREVIOUS ((size_t)-1)

/*
 * The arena itself.  The alignment specifier makes the first payload
 * - which starts one rounded-up header into the arena - suitably
 * aligned for any object.
 */
static _Alignas(max_align_t) unsigned char stdoc_heap_arena[STDOC_HEAP_SIZE];

static int    stdoc_heap_ready;
static size_t stdoc_heap_header;    /* header size, rounded up to the alignment */
static size_t stdoc_heap_used_bytes;
static size_t stdoc_heap_used_blocks;

/*
 * Round "size" up to a multiple of the alignment.  Returns 0 on
 * success and -1 when the rounded value would wrap around, which is
 * the overflow case a huge request hits.
 */
static int stdoc_heap_align_size(size_t size, size_t *p_aligned)
{
    size_t pad;

    pad = size % STDOC_HEAP_ALIGNMENT;

    if (pad == 0)
    {
        *p_aligned = size;
        return 0;
    }

    pad = STDOC_HEAP_ALIGNMENT - pad;

    if (size > (SIZE_MAX - pad))
    {
        return -1;
    }

    *p_aligned = size + pad;
    return 0;
}

/*
 * Move one header between the arena and a local structure.  See the
 * strict-aliasing note at the top of this file.
 */
static void stdoc_heap_block_load(size_t offset, stdoc_heap_block *p_block)
{
    (void)stdoc_memcpy(p_block, &stdoc_heap_arena[offset], sizeof *p_block);
}

static void stdoc_heap_block_store(size_t offset,
                                   const stdoc_heap_block *p_block)
{
    (void)stdoc_memcpy(&stdoc_heap_arena[offset], p_block, sizeof *p_block);
}

/*
 * Turn the arena into one free block the first time the heap is used.
 */
static void stdoc_heap_initialise(void)
{
    stdoc_heap_block first;

    if (stdoc_heap_ready != 0)
    {
        return;
    }

    stdoc_heap_header =
        ((sizeof(stdoc_heap_block) + (STDOC_HEAP_ALIGNMENT - 1u))
         / STDOC_HEAP_ALIGNMENT) * STDOC_HEAP_ALIGNMENT;

    first.payload = ((size_t)STDOC_HEAP_SIZE) - stdoc_heap_header;
    first.state = STDOC_HEAP_STATE_FREE;
    first.magic = STDOC_HEAP_MAGIC;

    stdoc_heap_block_store(0, &first);

    stdoc_heap_ready = 1;
}

/*
 * Find the block that starts at "offset" on the current list.
 *
 * Returns 0 and stores the block in "*p_block" when it is there, and
 * -1 otherwise, which includes a list whose size fields do not add up
 * - the walk stops instead of reading past the arena.
 *
 * The start of the block before the one found is reported in
 * "*p_previous", or STDOC_HEAP_NO_PREVIOUS when the block is the first
 * one.  Reporting it here is what lets a release merge with the block
 * before without walking the arena a second time; every block the walk
 * steps over has its size fields validated before they are trusted.
 */
static int stdoc_heap_find_block(size_t offset, stdoc_heap_block *p_block,
                                 size_t *p_previous)
{
    size_t cursor;
    size_t previous;

    cursor = 0;
    previous = STDOC_HEAP_NO_PREVIOUS;

    while (cursor < ((size_t)STDOC_HEAP_SIZE))
    {
        stdoc_heap_block block;

        if (cursor > (((size_t)STDOC_HEAP_SIZE) - stdoc_heap_header))
        {
            return -1;
        }

        stdoc_heap_block_load(cursor, &block);

        if (((block.payload % STDOC_HEAP_ALIGNMENT) != 0) ||
            (block.payload > (((size_t)STDOC_HEAP_SIZE) - cursor -
                              stdoc_heap_header)))
        {
            return -1;
        }

        if (cursor == offset)
        {
            *p_block = block;
            *p_previous = previous;
            return 0;
        }

        previous = cursor;
        cursor = cursor + stdoc_heap_header + block.payload;
    }

    return -1;
}

/*
 * Release the block that "p" points at the start of.
 *
 * Returns STDOC_HEAP_ACCEPT when the block was released (also for a
 * null pointer, which releases nothing), and one of the classification
 * values otherwise without having modified anything.
 *
 * The block list is walked once: the walk that finds the block also
 * reports the one before it, and the blocks after it begin where the
 * merged one ends, so the free neighbours are merged in the same pass.
 * Everything is validated before the first byte is written, so a list
 * that does not add up is reported with the arena still untouched.
 */
static int stdoc_heap_release(void *p)
{
    uintptr_t        address;
    uintptr_t        base;
    size_t           offset;
    size_t           block_offset;
    size_t           previous_offset;
    size_t           next_offset;
    size_t           released_payload;
    size_t           merged_payload;
    int              merge_previous;
    stdoc_heap_block block;

    if (p == NULL)
    {
        return STDOC_HEAP_ACCEPT;
    }

    address = (uintptr_t)p;
    base = (uintptr_t)stdoc_heap_arena;

    /*
     * The range check is done on addresses before any pointer
     * arithmetic, so a pointer from outside the arena never produces
     * a difference between unrelated pointers.
     */
    if ((address < (base + (uintptr_t)stdoc_heap_header)) ||
        (address >= (base + (uintptr_t)STDOC_HEAP_SIZE)))
    {
        return STDOC_HEAP_FOREIGN;
    }

    offset = (size_t)(address - base);
    block_offset = offset - stdoc_heap_header;

    if ((block_offset % STDOC_HEAP_ALIGNMENT) != 0)
    {
        return STDOC_HEAP_FOREIGN;
    }

    if (stdoc_heap_find_block(block_offset, &block, &previous_offset) != 0)
    {
        /*
         * No block starts here.  A header that a merge absorbed still
         * reads as free, which is how the double free of a block that
         * is no longer on the list is recognised; every other case is
         * a pointer into the middle of a block.
         */
        stdoc_heap_block_load(block_offset, &block);

        if ((block.magic == STDOC_HEAP_MAGIC) &&
            (block.state == STDOC_HEAP_STATE_FREE))
        {
            return STDOC_HEAP_DOUBLE;
        }

        return STDOC_HEAP_INTERIOR;
    }

    if (block.magic != STDOC_HEAP_MAGIC)
    {
        return STDOC_HEAP_CORRUPT;
    }

    if (block.state == STDOC_HEAP_STATE_FREE)
    {
        return STDOC_HEAP_DOUBLE;
    }

    if (block.state != STDOC_HEAP_STATE_USED)
    {
        return STDOC_HEAP_CORRUPT;
    }

    if (block.payload > (((size_t)STDOC_HEAP_SIZE) - block_offset -
                         stdoc_heap_header))
    {
        return STDOC_HEAP_CORRUPT;
    }

    released_payload = block.payload;
    merged_payload = block.payload;
    merge_previous = 0;

    /*
     * The free block before this one, when there is one.  It is the
     * block the walk stepped over, so its size fields were validated
     * on the way; a header that has lost its magic, or a state that is
     * neither free nor in use, means the list has lost its shape.
     */
    if (previous_offset != STDOC_HEAP_NO_PREVIOUS)
    {
        stdoc_heap_block previous;

        stdoc_heap_block_load(previous_offset, &previous);

        if (previous.magic != STDOC_HEAP_MAGIC)
        {
            return STDOC_HEAP_CORRUPT;
        }

        if (previous.state != STDOC_HEAP_STATE_USED)
        {
            if (previous.state != STDOC_HEAP_STATE_FREE)
            {
                return STDOC_HEAP_CORRUPT;
            }

            merge_previous = 1;
            merged_payload = merged_payload + stdoc_heap_header +
                             previous.payload;
        }
    }

    /*
     * The free blocks after this one.  They begin where the released
     * block ends - the absorbed block before it reaches further back,
     * not further forward - so the scan starts from the released
     * payload.  The invariant of the list says at most one free block
     * can be there, but the loop leaves the list merged even if some
     * earlier fault left a run behind, and every block it considers is
     * validated before its size is trusted.
     */
    next_offset = block_offset + stdoc_heap_header + released_payload;

    while (next_offset < ((size_t)STDOC_HEAP_SIZE))
    {
        stdoc_heap_block next;

        if (next_offset > (((size_t)STDOC_HEAP_SIZE) - stdoc_heap_header))
        {
            return STDOC_HEAP_CORRUPT;
        }

        stdoc_heap_block_load(next_offset, &next);

        if ((next.magic != STDOC_HEAP_MAGIC) ||
            ((next.state != STDOC_HEAP_STATE_FREE) &&
             (next.state != STDOC_HEAP_STATE_USED)) ||
            ((next.payload % STDOC_HEAP_ALIGNMENT) != 0) ||
            (next.payload > (((size_t)STDOC_HEAP_SIZE) - next_offset -
                             stdoc_heap_header)))
        {
            return STDOC_HEAP_CORRUPT;
        }

        if (next.state == STDOC_HEAP_STATE_USED)
        {
            break;
        }

        merged_payload = merged_payload + stdoc_heap_header + next.payload;
        next_offset = next_offset + stdoc_heap_header + next.payload;
    }

    /*
     * Everything adds up, so the arena may now be changed.  The
     * released block is marked free on its own first: when the block
     * before it is absorbed, its header stays behind as the stale
     * bytes that make a later double free of the same pointer
     * recognisable instead of looking like a pointer into a payload.
     */
    block.payload = released_payload;
    block.state = STDOC_HEAP_STATE_FREE;
    stdoc_heap_block_store(block_offset, &block);

    if (merge_previous != 0)
    {
        block.payload = merged_payload;
        stdoc_heap_block_store(previous_offset, &block);
    }
    else if (merged_payload != released_payload)
    {
        block.payload = merged_payload;
        stdoc_heap_block_store(block_offset, &block);
    }

    /*
     * The accounting drops the payload of the released block alone: a
     * free block that was absorbed never had an entry in it.
     */
    stdoc_heap_used_bytes = stdoc_heap_used_bytes - released_payload;
    stdoc_heap_used_blocks = stdoc_heap_used_blocks - 1u;

    return STDOC_HEAP_ACCEPT;
}

/*
 * Report an unrecoverable heap failure and terminate the process.
 *
 * The diagnostic goes to stdoc_stderr, which is unbuffered, so it
 * reaches the operating system before the exit system call.
 */
static void stdoc_heap_fatal(const char *p_function, const char *p_reason)
    __attribute__((noreturn));

static void stdoc_heap_fatal(const char *p_function, const char *p_reason)
{
    (void)stdoc_fprintf(stdoc_stderr, "libstdoc: %s: %s\n",
                        p_function, p_reason);

    stdoc_sys_exit(STDOC_HEAP_FATAL_STATUS);
}

/*
 * Human-readable form of a classification value.
 */
static const char *stdoc_heap_explain(int result)
{
    switch (result)
    {
        case STDOC_HEAP_FOREIGN:
            return "pointer is outside the libstdoc heap";
        case STDOC_HEAP_INTERIOR:
            return "pointer is not the start of a heap block";
        case STDOC_HEAP_DOUBLE:
            return "block is already free (double free)";
        default:
            return "heap block is corrupted";
    }
}

void *stdoc_malloc(size_t size)
{
    size_t requested;
    size_t cursor;

    if (size == 0)
    {
        return NULL;
    }

    stdoc_heap_initialise();

    if (stdoc_heap_align_size(size, &requested) != 0)
    {
        return NULL;
    }

    if (requested > (((size_t)STDOC_HEAP_SIZE) - stdoc_heap_header))
    {
        return NULL;
    }

    cursor = 0;

    while (cursor < ((size_t)STDOC_HEAP_SIZE))
    {
        stdoc_heap_block block;

        if (cursor > (((size_t)STDOC_HEAP_SIZE) - stdoc_heap_header))
        {
            return NULL;
        }

        stdoc_heap_block_load(cursor, &block);

        if (((block.payload % STDOC_HEAP_ALIGNMENT) != 0) ||
            (block.payload > (((size_t)STDOC_HEAP_SIZE) - cursor -
                              stdoc_heap_header)))
        {
            return NULL;
        }

        if ((block.state == STDOC_HEAP_STATE_FREE) &&
            (block.payload >= requested))
        {
            size_t spare;

            spare = block.payload - requested;
            block.state = STDOC_HEAP_STATE_USED;

            if (spare >= (stdoc_heap_header + STDOC_HEAP_ALIGNMENT))
            {
                stdoc_heap_block tail;

                tail.payload = spare - stdoc_heap_header;
                tail.state = STDOC_HEAP_STATE_FREE;
                tail.magic = STDOC_HEAP_MAGIC;

                block.payload = requested;

                stdoc_heap_block_store(cursor, &block);
                stdoc_heap_block_store(cursor + stdoc_heap_header +
                                       requested, &tail);
            }
            else
            {
                /* The leftover is too small to hold a block: keep it. */
                stdoc_heap_block_store(cursor, &block);
            }

            stdoc_heap_used_bytes = stdoc_heap_used_bytes + block.payload;
            stdoc_heap_used_blocks = stdoc_heap_used_blocks + 1u;

            return &stdoc_heap_arena[cursor + stdoc_heap_header];
        }

        cursor = cursor + stdoc_heap_header + block.payload;
    }

    return NULL;
}

void stdoc_free(void *p)
{
    if (p == NULL)
    {
        return;
    }

    stdoc_heap_initialise();

    /*
     * The unsafe entry point is forgiving by design: a pointer it
     * does not recognise is ignored rather than acted upon, and the
     * safe entry point below is the one that reports it.
     */
    (void)stdoc_heap_release(p);
}

void *stdoc_malloc_safe(size_t count, size_t size)
{
    void  *p;
    size_t total;

    if ((count != 0) && (size > (SIZE_MAX / count)))
    {
        stdoc_heap_fatal("stdoc_malloc_safe", "allocation size overflows");
    }

    total = count * size;
    p = NULL;

    if (total != 0)
    {
        p = stdoc_malloc(total);

        if (p == NULL)
        {
            stdoc_heap_fatal("stdoc_malloc_safe", "out of memory");
        }

        (void)stdoc_memset(p, 0, total);
    }

    return p;
}

void stdoc_free_safe(void *p)
{
    int result;

    if (p == NULL)
    {
        return;
    }

    stdoc_heap_initialise();

    result = stdoc_heap_release(p);

    if (result == STDOC_HEAP_ACCEPT)
    {
        return;
    }

    stdoc_heap_fatal("stdoc_free_safe", stdoc_heap_explain(result));
}

size_t stdoc_heap_live_bytes(void)
{
    return stdoc_heap_used_bytes;
}

size_t stdoc_heap_live_blocks(void)
{
    return stdoc_heap_used_blocks;
}

size_t stdoc_heap_capacity(void)
{
    return (size_t)STDOC_HEAP_SIZE;
}
