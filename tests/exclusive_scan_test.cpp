/*******************************************************************************
 * tests/exclusive_scan_test.cpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2018 Michael Axtmann <michael.axtmann@kit.edu>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#include <iterator>
#include <vector>
#include <functional>

#include <tlx/algorithm.hpp>
#include <tlx/die.hpp>
#include <tlx/logger.hpp>

#include <iostream>


static void exclusive_scan() {
    {
        const std::vector<int> vec1;
        std::vector<int> vec2(1);
        int init = 1;
        const auto res1 = tlx::exclusive_scan(vec1.begin(), vec1.end(), vec2.begin(), init);
        std::cout << vec2.end() - res1 << std::endl;
        std::cout << vec2.front() << std::endl;
        die_unless(res1 == vec2.end());
        die_unless(vec2.front() == init);
    }

    {
        const std::vector<int> vec1 = {1,2,3};
        std::vector<int> vec2(5);
        const std::vector<int> vec_res = {1, 2, 4, 7};
        int init = 1;
        const auto res1 = tlx::exclusive_scan(vec1.begin(), vec1.end(), vec2.begin(), init);
        die_unless(res1 == vec2.begin() + 4);
        die_unless(std::equal(vec_res.begin(), vec_res.end(), vec2.begin()));
    }

    {
        const std::vector<int> vec1 = {1,2,3};
        std::vector<int> vec2(5);
        const std::vector<int> vec_res = {1, 0, -2, -5};
        int init = 1;
        const auto binary_op = std::minus<int>();
        const auto res1 = tlx::exclusive_scan(vec1.begin(), vec1.end(), vec2.begin(), init, binary_op);
        die_unless(res1 == vec2.begin() + 4);
        die_unless(std::equal(vec_res.begin(), vec_res.end(), vec2.begin()));
    }
}

int main() {

    exclusive_scan();

    return 0;
}

/******************************************************************************/
