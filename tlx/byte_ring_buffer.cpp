/*******************************************************************************
 * tlx/byte_ring_buffer.cpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2010-2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#include <tlx/byte_ring_buffer.hpp>

#include <algorithm>
#include <cassert>

namespace tlx {

ByteRingBuffer::ByteRingBuffer()
    : data_(nullptr), buff_size_(0), size_(0), bottom_(0) { }

ByteRingBuffer::~ByteRingBuffer() {
    if (data_) free(data_);
}

size_t ByteRingBuffer::size() const {
    return size_;
}

size_t ByteRingBuffer::buff_size() const {
    return buff_size_;
}

void ByteRingBuffer::clear() {
    size_ = bottom_ = 0;
}

char* ByteRingBuffer::bottom() const {
    return data_ + bottom_;
}

size_t ByteRingBuffer::bottom_size() const {
    return (bottom_ + size_ > buff_size_)
           ? (buff_size_ - bottom_)
           : (size_);
}

void ByteRingBuffer::advance(size_t n) {
    assert(size_ >= n);
    bottom_ += n;
    size_ -= n;
    if (bottom_ >= buff_size_) bottom_ -= buff_size_;
}

void ByteRingBuffer::write(const void* src, size_t len) {
    if (len == 0) return;

    const char* csrc = reinterpret_cast<const char*>(src);

    if (buff_size_ < size_ + len)
    {
        // won't fit, we have to grow the buffer, we'll grow the buffer to
        // twice the size.

        size_t newbuffsize = buff_size_;
        while (newbuffsize < size_ + len)
        {
            if (newbuffsize == 0) newbuffsize = 1024;
            else newbuffsize = newbuffsize * 2;
        }

        data_ = static_cast<char*>(realloc(data_, newbuffsize));

        if (bottom_ + size_ > buff_size_)
        {
            // copy the ringbuffer's tail to the new buffer end, use memcpy
            // here because there cannot be any overlapping area.

            size_t taillen = buff_size_ - bottom_;

            std::copy(data_ + bottom_, data_ + bottom_ + taillen,
                      data_ + newbuffsize - taillen);

            bottom_ = newbuffsize - taillen;
        }

        buff_size_ = newbuffsize;
    }

    // block now fits into the buffer somehow

    // check if the new memory fits into the middle space
    if (bottom_ + size_ > buff_size_)
    {
        std::copy(csrc, csrc + len,
                  data_ + bottom_ + size_ - buff_size_);
        size_ += len;
    }
    else
    {
        // first fill up the buffer's tail, which has tailfit bytes room
        size_t tailfit = buff_size_ - (bottom_ + size_);

        if (tailfit >= len)
        {
            std::copy(csrc, csrc + len,
                      data_ + bottom_ + size_);
            size_ += len;
        }
        else
        {
            // doesn't fit into the tail alone, we have to break it up
            std::copy(csrc, csrc + tailfit,
                      data_ + bottom_ + size_);
            std::copy(csrc + tailfit, csrc + len,
                      data_);
            size_ += len;
        }
    }
}

} // namespace tlx

/******************************************************************************/
