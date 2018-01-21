/*******************************************************************************
 * tests/byte_ring_buffer_test.cpp
 *
 * Extensive test case for internal ByteRingBuffer. Covers both external and
 * internal states.
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2010-2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#include <tlx/byte_ring_buffer.hpp>

#include <tlx/die.hpp>

void test1() {
    tlx::ByteRingBuffer bp;

    for (size_t i = 0; i < 128; ++i)
        bp.write(&i, sizeof(i));

    die_unequal(bp.size(), 128 * sizeof(size_t));

    for (size_t i = 0; i < 128; ++i)
    {
        die_unless(bp.bottom_size() >= sizeof(size_t));
        die_unequal(*reinterpret_cast<size_t*>(bp.bottom()), i);

        bp.advance(sizeof(size_t));
    }

    die_unequal(bp.size(), 0u);
    die_unequal(bp.bottom_size(), 0u);
    die_unequal(bp.buff_size(), 1024u);

    for (size_t i = 0; i < 512; ++i)
    {
        die_unequal(bp.size(), 0u);
        die_unequal(bp.bottom_size(), 0u);

        bp.write(&i, sizeof(i));

        die_unequal(bp.bottom_size(), sizeof(size_t));
        die_unequal(*reinterpret_cast<size_t*>(bp.bottom()), i);

        bp.advance(sizeof(size_t));

        tlx::ByteRingBuffer cbp = bp;

        die_unequal(cbp.size(), bp.size());
        die_unequal(cbp.bottom_size(), bp.bottom_size());
    }
}

void test2() {
    char buffer[2048];

    for (size_t i = 0; i < sizeof(buffer); ++i)
        buffer[i] = i;

    // test growth in first buffer state
    {
        tlx::ByteRingBuffer bp;

        bp.write(buffer, 256);
        bp.advance(256);

        die_unequal(bp.size(), 0u);
        die_unequal(bp.bottom_size(), 0u);
        die_unequal(bp.buff_size(), 1024u);

        bp.write(buffer, 512);

        die_unequal(bp.size(), 512u);
        die_unequal(bp.bottom_size(), 512u);
        die_unequal(bp.buff_size(), 1024u);

        bp.write(buffer, 1024);

        die_unequal(bp.size(), 512u + 1024u);
        die_unequal(bp.bottom_size(), 512u + 1024u);
        die_unequal(bp.buff_size(), 2048u);
    }

    // test growth in second buffer state
    {
        tlx::ByteRingBuffer bp;

        bp.write(buffer, 512 + 256);
        bp.advance(512 + 256);

        die_unequal(bp.size(), 0u);
        die_unequal(bp.bottom_size(), 0u);
        die_unequal(bp.buff_size(), 1024u);

        bp.write(buffer, 512);

        die_unequal(bp.size(), 512u);
        die_unequal(bp.bottom_size(), 256u);
        die_unequal(bp.buff_size(), 1024u);

        bp.write(buffer, 1024);

        die_unequal(bp.size(), 512u + 1024u);
        die_unequal(bp.bottom_size(), 256u);
        die_unequal(bp.buff_size(), 2048u);
    }
}

int main() {
    test1();
    test2();

    return 0;
}

/******************************************************************************/
