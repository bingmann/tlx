/*******************************************************************************
 * tlx/byte_ring_buffer.hpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2010-2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#ifndef TLX_BYTE_RING_BUFFER_HEADER
#define TLX_BYTE_RING_BUFFER_HEADER

#include <cstdlib>

namespace tlx {

//! \addtogroup tlx_data_structures
//! \{

/*!
 * ByteRingBuffer is a byte-oriented, auto-growing, pipe memory buffer which
 * uses the underlying space in a circular fashion.
 *
 * The input stream is write()en into the buffer as blocks of bytes, while the
 * buffer is reallocated with exponential growth as needed. Be warned that the
 * pipe is not thread-safe.
 *
 * The first unread byte can be accessed using bottom(). The number of unread
 * bytes at the ring buffers bottom position is queried by bottom_size(). This
 * may not match the total number of unread bytes as returned by size(). After
 * processing the bytes at bottom(), the unread cursor may be moved using
 * advance().
 *
 * The ring buffer has the following two states.
 * <pre>
 * +------------------------------------------------------------------+
 * | unused     |                 data   |               unused       |
 * +------------+------------------------+----------------------------+
 *              ^                        ^
 *              bottom_                  bottom_+size_
 * </pre>
 *
 * or
 *
 * <pre>
 * +------------------------------------------------------------------+
 * | more data  |                 unused               | data         |
 * +------------+--------------------------------------+--------------+
 *              ^                                      ^
 *              bottom_+size_                          bottom_
 * </pre>
 *
 * The size of the whole buffer is buff_size().
 */
class ByteRingBuffer
{
public:
    //! Construct an empty ring buffer.
    ByteRingBuffer();

    //! Free the possibly used memory space.
    ~ByteRingBuffer();

    //! Return the current number of unread bytes.
    size_t size() const;

    //! Return the current number of allocated bytes.
    size_t buff_size() const;

    //! Reset the ring buffer to empty.
    void clear();

    /*!
     * Return a pointer to the first unread element. Be warned that the buffer
     * may not be linear, thus bottom()+size() might not be valid. You have to
     * use bottom_size().
     */
    char * bottom() const;

    //! Return the number of bytes available at the bottom() place.
    size_t bottom_size() const;

    /*!
     * Advance the internal read pointer n bytes, thus marking that amount of
     * data as read.
     */
    void advance(size_t n);

    /*!
     * Write len bytes into the ring buffer at the top position, the buffer will
     * grow if necessary.
     */
    void write(const void* src, size_t len);

private:
    //! pointer to allocated memory buffer
    char* data_;

    //! number of bytes allocated in data_
    size_t buff_size_;

    //! number of unread bytes in ring buffer
    size_t size_;

    //! bottom pointer of unread area
    size_t bottom_;
};

//! \}

} // namespace tlx

#endif // !TLX_BYTE_RING_BUFFER_HEADER

/******************************************************************************/
