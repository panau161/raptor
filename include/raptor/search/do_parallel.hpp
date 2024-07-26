// SPDX-FileCopyrightText: 2006-2024 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2024 Knut Reinert & MPI für molekulare Genetik
// SPDX-License-Identifier: BSD-3-Clause

/*!\file
 * \brief Provides raptor::do_parallel.
 * \author Enrico Seiler <enrico.seiler AT fu-berlin.de>
 */

#pragma once

#include <algorithm>
#include <functional>
#include <omp.h>
#include <vector>

#include <hibf/misc/divide_and_ceil.hpp>

namespace raptor
{

template <typename algorithm_t>
void do_parallel(algorithm_t && worker, size_t const num_records, size_t const threads)
{
#pragma omp parallel for schedule(guided) num_threads(threads)
    for (size_t i = 0; i < num_records; ++i)
    {
        std::invoke(worker, i);
    }
}

} // namespace raptor
