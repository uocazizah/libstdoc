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
 * Strict aliasing.  The arena is a declared "std::byte" array, so
 * inside this file it is only ever read and written through byte
 * lvalues; the block headers are moved in and out with stdoc_memcpy
 * rather than by casting the storage to a structure pointer.  What a
 * caller does with the payload it receives is the caller's own
 * effective type, exactly as with any other allocator.
 *
 * The public entry points carry the C linkage the headers declare, so
 * a C program links them under their unmangled names.
 *
 * Not thread-safe: libstdoc creates no threads and provides no
 * synchronisation.
 */

#include <cstddef>
#include <cstdint>

#include "stdoc_io.hxx"
#include "stdoc_mem.hxx"

namespace stdoc::mem
{

namespace
{

/* Size of the arena, as a value the compiler can use in a type. */
constexpr std::size_t stdoc_heap_size =
    static_cast<std::size_t>(STDOC_HEAP_SIZE);

/*
 * Payloads and headers are multiples of this alignment, which is the
 * strictest one the C library of the target can require.
 */
constexpr std::size_t stdoc_heap_alignment = alignof(std::max_align_t);

static_assert(stdoc_heap_size >= (4U * stdoc_heap_alignment),
              "STDOC_HEAP_SIZE is too small to hold a single heap block");

/*
 * Magic marking bytes as a header this allocator wrote, and the state
 * that tells a live block from a free one, which is how a double free
 * is told apart from a pointer that was never allocated.
 */
constexpr std::uint32_t stdoc_heap_magic = 0x53444F43U; /* "SDOC" */

enum class block_state : std::uint32_t
{
    free = 0U,
    used = 1U
};

/*
 * One block header.
 *
 * "payload" is the number of usable bytes after the header, always a
 * multiple of stdoc_heap_alignment.  sizeof of this structure is
 * rounded up to that alignment before it is used as an offset, so the
 * field layout may change without breaking the alignment of a
 * payload.  The state is a fixed-width enumeration for the same
 * reason: the header is written into raw arena bytes and has to mean
 * the same thing on every target.
 */
struct stdoc_heap_block
{
    std::size_t  payload;
    block_state  state;
    std::uint32_t magic;
};

/*
 * Exit status of the fail-fast paths of the safe functions.  134 is
 * the conventional status of an aborted process (128 + SIGABRT), so a
 * caller that watches the status of a child sees a familiar number.
 */
constexpr int stdoc_heap_fatal_status = 134;

/*
 * Classification of a pointer handed to stdoc_heap_release.
 */
enum class release_result
{
    accept,
    foreign,
    interior,
    double_free,
    corrupt
};

/*
 * Result of the "previous block" lookup when the block being looked
 * for is the first one in the arena.  A real block start is always
 * smaller than the arena, so this value cannot be mistaken for one.
 */
constexpr std::size_t stdoc_heap_no_previous =
    static_cast<std::size_t>(-1);

/*
 * The arena itself.  The alignment specifier makes the first payload
 * - which starts one rounded-up header into the arena - suitably
 * aligned for any object.  "constinit" holds it to the other
 * requirement of the freestanding startup path: the arena is
 * zero-initialized while the program is loaded, so nothing has to run
 * before the heap is usable, and an initializer that would change
 * that is rejected while the library is compiled instead of failing
 * silently at run time.
 */
alignas(std::max_align_t) constinit std::byte stdoc_heap_arena[stdoc_heap_size];

bool        stdoc_heap_ready;
std::size_t stdoc_heap_header;    /* header size, rounded up to the alignment */
std::size_t stdoc_heap_used_bytes;
std::size_t stdoc_heap_used_blocks;

/*
 * Round "size" up to a multiple of the alignment.  Returns 0 on
 * success and -1 when the rounded value would wrap around, which is
 * the overflow case a huge request hits.
 */
int stdoc_heap_align_size(std::size_t size, std::size_t *p_aligned)
{
    std::size_t pad;

    pad = size % stdoc_heap_alignment;

    if (pad == 0)
    {
        *p_aligned = size;
        return 0;
    }

    pad = stdoc_heap_alignment - pad;

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
void stdoc_heap_block_load(std::size_t offset, stdoc_heap_block *p_block)
{
    static_cast<void>(
        stdoc_memcpy(p_block, &stdoc_heap_arena[offset], sizeof *p_block));
}

void stdoc_heap_block_store(std::size_t offset,
                            const stdoc_heap_block *p_block)
{
    static_cast<void>(
        stdoc_memcpy(&stdoc_heap_arena[offset], p_block, sizeof *p_block));
}

/*
 * Turn the arena into one free block the first time the heap is used.
 */
void stdoc_heap_initialise(void)
{
    stdoc_heap_block first;

    if (stdoc_heap_ready)
    {
        return;
    }

    stdoc_heap_header =
        ((sizeof(stdoc_heap_block) + (stdoc_heap_alignment - 1U))
         / stdoc_heap_alignment) * stdoc_heap_alignment;

    first.payload = stdoc_heap_size - stdoc_heap_header;
    first.state = block_state::free;
    first.magic = stdoc_heap_magic;

    stdoc_heap_block_store(0, &first);

    stdoc_heap_ready = true;
}

/*
 * Find the block that starts at "offset" on the current list.
 *
 * Returns 0 and stores the block in "*p_block" when it is there, and
 * -1 otherwise, which includes a list whose size fields do not add up
 * - the walk stops instead of reading past the arena.
 *
 * The start of the block before the one found is reported in
 * "*p_previous", or stdoc_heap_no_previous when the block is the first
 * one.  Reporting it here is what lets a release merge with the block
 * before without walking the arena a second time; every block the walk
 * steps over has its size fields validated before they are trusted.
 */
int stdoc_heap_find_block(std::size_t offset, stdoc_heap_block *p_block,
                          std::size_t *p_previous)
{
    std::size_t cursor;
    std::size_t previous;

    cursor = 0;
    previous = stdoc_heap_no_previous;

    while (cursor < stdoc_heap_size)
    {
        stdoc_heap_block block;

        if (cursor > (stdoc_heap_size - stdoc_heap_header))
        {
            return -1;
        }

        stdoc_heap_block_load(cursor, &block);

        if (((block.payload % stdoc_heap_alignment) != 0) ||
            (block.payload > (stdoc_heap_size - cursor -
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
 * Returns release_result::accept when the block was released (also for
 * a null pointer, which releases nothing), and one of the
 * classification values otherwise without having modified anything.
 *
 * The block list is walked once: the walk that finds the block also
 * reports the one before it, and the blocks after it begin where the
 * merged one ends, so the free neighbours are merged in the same pass.
 * Everything is validated before the first byte is written, so a list
 * that does not add up is reported with the arena still untouched.
 */
release_result stdoc_heap_release(void *p)
{
    std::uintptr_t   address;
    std::uintptr_t   base;
    std::size_t      offset;
    std::size_t      block_offset;
    std::size_t      previous_offset;
    std::size_t      next_offset;
    std::size_t      released_payload;
    std::size_t      merged_payload;
    bool             merge_previous;
    stdoc_heap_block block;

    if (p == nullptr)
    {
        return release_result::accept;
    }

    address = reinterpret_cast<std::uintptr_t>(p);
    base = reinterpret_cast<std::uintptr_t>(stdoc_heap_arena);

    /*
     * The range check is done on addresses before any pointer
     * arithmetic, so a pointer from outside the arena never produces
     * a difference between unrelated pointers.
     */
    if ((address < (base + static_cast<std::uintptr_t>(stdoc_heap_header))) ||
        (address >= (base + static_cast<std::uintptr_t>(stdoc_heap_size))))
    {
        return release_result::foreign;
    }

    offset = static_cast<std::size_t>(address - base);
    block_offset = offset - stdoc_heap_header;

    if ((block_offset % stdoc_heap_alignment) != 0)
    {
        return release_result::foreign;
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

        if ((block.magic == stdoc_heap_magic) &&
            (block.state == block_state::free))
        {
            return release_result::double_free;
        }

        return release_result::interior;
    }

    if (block.magic != stdoc_heap_magic)
    {
        return release_result::corrupt;
    }

    if (block.state == block_state::free)
    {
        return release_result::double_free;
    }

    if (block.state != block_state::used)
    {
        return release_result::corrupt;
    }

    if (block.payload > (stdoc_heap_size - block_offset - stdoc_heap_header))
    {
        return release_result::corrupt;
    }

    released_payload = block.payload;
    merged_payload = block.payload;
    merge_previous = false;

    /*
     * The free block before this one, when there is one.  It is the
     * block the walk stepped over, so its size fields were validated
     * on the way; a header that has lost its magic, or a state that is
     * neither free nor in use, means the list has lost its shape.
     */
    if (previous_offset != stdoc_heap_no_previous)
    {
        stdoc_heap_block previous;

        stdoc_heap_block_load(previous_offset, &previous);

        if (previous.magic != stdoc_heap_magic)
        {
            return release_result::corrupt;
        }

        if (previous.state != block_state::used)
        {
            if (previous.state != block_state::free)
            {
                return release_result::corrupt;
            }

            merge_previous = true;
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

    while (next_offset < stdoc_heap_size)
    {
        stdoc_heap_block next;

        if (next_offset > (stdoc_heap_size - stdoc_heap_header))
        {
            return release_result::corrupt;
        }

        stdoc_heap_block_load(next_offset, &next);

        if ((next.magic != stdoc_heap_magic) ||
            ((next.state != block_state::free) &&
             (next.state != block_state::used)) ||
            ((next.payload % stdoc_heap_alignment) != 0) ||
            (next.payload > (stdoc_heap_size - next_offset -
                             stdoc_heap_header)))
        {
            return release_result::corrupt;
        }

        if (next.state == block_state::used)
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
    block.state = block_state::free;
    stdoc_heap_block_store(block_offset, &block);

    if (merge_previous)
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
    stdoc_heap_used_blocks = stdoc_heap_used_blocks - 1U;

    return release_result::accept;
}

/*
 * Report an unrecoverable heap failure and terminate the process.
 *
 * The diagnostic goes to stdoc_stderr, which is unbuffered, so it
 * reaches the operating system before the exit system call.
 */
[[noreturn]] void stdoc_heap_fatal(const char *p_function,
                                   const char *p_reason)
{
    static_cast<void>(stdoc_fprintf(stdoc_stderr, "libstdoc: %s: %s\n",
                                    p_function, p_reason));

    stdoc_sys_exit(stdoc_heap_fatal_status);
}

/*
 * Human-readable form of a classification value.
 */
const char *stdoc_heap_explain(release_result result)
{
    switch (result)
    {
        case release_result::foreign:
            return "pointer is outside the libstdoc heap";
        case release_result::interior:
            return "pointer is not the start of a heap block";
        case release_result::double_free:
            return "block is already free (double free)";
        default:
            return "heap block is corrupted";
    }
}

} /* namespace */

extern "C" void *stdoc_malloc(std::size_t size)
{
    std::size_t requested;
    std::size_t cursor;

    if (size == 0)
    {
        return nullptr;
    }

    stdoc_heap_initialise();

    if (stdoc_heap_align_size(size, &requested) != 0)
    {
        return nullptr;
    }

    if (requested > (stdoc_heap_size - stdoc_heap_header))
    {
        return nullptr;
    }

    cursor = 0;

    while (cursor < stdoc_heap_size)
    {
        stdoc_heap_block block;

        if (cursor > (stdoc_heap_size - stdoc_heap_header))
        {
            return nullptr;
        }

        stdoc_heap_block_load(cursor, &block);

        if (((block.payload % stdoc_heap_alignment) != 0) ||
            (block.payload > (stdoc_heap_size - cursor -
                              stdoc_heap_header)))
        {
            return nullptr;
        }

        if ((block.state == block_state::free) &&
            (block.payload >= requested))
        {
            std::size_t spare;

            spare = block.payload - requested;
            block.state = block_state::used;

            if (spare >= (stdoc_heap_header + stdoc_heap_alignment))
            {
                stdoc_heap_block tail;

                tail.payload = spare - stdoc_heap_header;
                tail.state = block_state::free;
                tail.magic = stdoc_heap_magic;

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
            stdoc_heap_used_blocks = stdoc_heap_used_blocks + 1U;

            return &stdoc_heap_arena[cursor + stdoc_heap_header];
        }

        cursor = cursor + stdoc_heap_header + block.payload;
    }

    return nullptr;
}

extern "C" void stdoc_free(void *p)
{
    if (p == nullptr)
    {
        return;
    }

    stdoc_heap_initialise();

    /*
     * The unsafe entry point is forgiving by design: a pointer it
     * does not recognise is ignored rather than acted upon, and the
     * safe entry point below is the one that reports it.
     */
    static_cast<void>(stdoc_heap_release(p));
}

extern "C" void *stdoc_malloc_safe(std::size_t count, std::size_t size)
{
    void       *p;
    std::size_t total;

    if ((count != 0) && (size > (SIZE_MAX / count)))
    {
        stdoc_heap_fatal("stdoc_malloc_safe", "allocation size overflows");
    }

    total = count * size;
    p = nullptr;

    if (total != 0)
    {
        p = stdoc_malloc(total);

        if (p == nullptr)
        {
            stdoc_heap_fatal("stdoc_malloc_safe", "out of memory");
        }

        static_cast<void>(stdoc_memset(p, 0, total));
    }

    return p;
}

extern "C" void stdoc_free_safe(void *p)
{
    release_result result;

    if (p == nullptr)
    {
        return;
    }

    stdoc_heap_initialise();

    result = stdoc_heap_release(p);

    if (result == release_result::accept)
    {
        return;
    }

    stdoc_heap_fatal("stdoc_free_safe", stdoc_heap_explain(result));
}

extern "C" std::size_t stdoc_heap_live_bytes(void)
{
    return stdoc_heap_used_bytes;
}

extern "C" std::size_t stdoc_heap_live_blocks(void)
{
    return stdoc_heap_used_blocks;
}

extern "C" std::size_t stdoc_heap_capacity(void)
{
    return stdoc_heap_size;
}

} /* namespace stdoc::mem */
