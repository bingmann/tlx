/*******************************************************************************
 * tlx/algorithm.hpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#ifndef TLX_ALGORITHM_HEADER
#define TLX_ALGORITHM_HEADER

//! \defgroup tlx_algorithm Algorithms
//! Algorithms for iterators and ranges

/*[[[perl
print "#include <$_>\n" foreach sort glob("tlx/algorithm/"."*.hpp");
]]]*/
#include <tlx/algorithm/is_sorted_cmp.hpp>
#include <tlx/algorithm/merge_combine.hpp>
#include <tlx/algorithm/exclusive_scan.hpp>
// [[[end]]]

#endif // !TLX_ALGORITHM_HEADER

/******************************************************************************/
