/*******************************************************************************
 * tlx/unused.hpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2015-2017 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#ifndef TLX_UNUSED_HEADER
#define TLX_UNUSED_HEADER

namespace tlx {

/******************************************************************************/
// tlx::unused(variables...)

template <typename... Types>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
void unused(Types&&... /*xs*/)
{
}

} // namespace tlx

#endif // !TLX_UNUSED_HEADER

/******************************************************************************/
