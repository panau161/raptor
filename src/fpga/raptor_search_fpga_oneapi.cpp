// SPDX-FileCopyrightText: 2006-2025 Knut Reinert & Freie Universität Berlin
// SPDX-FileCopyrightText: 2016-2025 Knut Reinert & MPI für molekulare Genetik
// SPDX-FileCopyrightText: 2020-2025 Thomas Steinke & Zuse Institute Berlin
// SPDX-License-Identifier: BSD-3-Clause

#include <cmath>
#include <fstream>
#include <vector>

#include <hibf/misc/unreachable.hpp>

#include <raptor/argument_parsing/search_arguments.hpp>
#include <raptor/fpga/min_ibf_fpga_oneapi.hpp>
#include <raptor/threshold/threshold.hpp>

#ifndef FPGA_BINS
#    error "FPGA_BINS is not defined."
#endif

#define RAPTOR_CASE_MACRO(VALUE)                                                                                       \
    case VALUE:                                                                                                        \
        process.template operator()<VALUE>();                                                                          \
        break;

// https://www.scs.stanford.edu/~dm/blog/va-opt.html#the-for_each-macro
#define PARENS ()

#define RAPTOR_EXPAND(...) RAPTOR_EXPAND4(RAPTOR_EXPAND4(RAPTOR_EXPAND4(RAPTOR_EXPAND4(__VA_ARGS__))))
#define RAPTOR_EXPAND4(...) RAPTOR_EXPAND3(RAPTOR_EXPAND3(RAPTOR_EXPAND3(RAPTOR_EXPAND3(__VA_ARGS__))))
#define RAPTOR_EXPAND3(...) RAPTOR_EXPAND2(RAPTOR_EXPAND2(RAPTOR_EXPAND2(RAPTOR_EXPAND2(__VA_ARGS__))))
#define RAPTOR_EXPAND2(...) RAPTOR_EXPAND1(RAPTOR_EXPAND1(RAPTOR_EXPAND1(RAPTOR_EXPAND1(__VA_ARGS__))))
#define RAPTOR_EXPAND1(...) __VA_ARGS__

#define RAPTOR_FOR_EACH(macro, ...) __VA_OPT__(RAPTOR_EXPAND(RAPTOR_FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define RAPTOR_FOR_EACH_HELPER(macro, a1, ...) macro(a1) __VA_OPT__(RAPTOR_FOR_EACH_AGAIN PARENS(macro, __VA_ARGS__))
#define RAPTOR_FOR_EACH_AGAIN() RAPTOR_FOR_EACH_HELPER

class fpga_thresholder : raptor::threshold::threshold
{
private:
    using base_t = raptor::threshold::threshold;

public:
    using base_t::base_t;

    std::pair<size_t, size_t> get_minmax() const
    {
        return std::make_pair(minimal_number_of_minimizers, maximal_number_of_minimizers);
    }

    std::vector<size_t> get_thresholds() const
    {
        if (threshold_kind != threshold_kinds::probabilistic)
            throw std::runtime_error{"Only probabilistic thresholds are supported."}; // TODO: argparse

        std::vector<size_t> result;

        std::ranges::transform(precomp_thresholds,
                               precomp_correction,
                               std::back_inserter(result),
                               [](size_t const threshold, size_t const correction)
                               {
                                   return std::max<size_t>(1u, threshold + correction);
                               });

        return result;
    }
};

void raptor_search_fpga_oneapi(raptor::search_arguments const & arguments)
{
    std::vector<size_t> thresholds;
    size_t minimal_number_of_minimizers{};
    size_t maximal_number_of_minimizers{};
    {
        fpga_thresholder thresholder{arguments.make_threshold_parameters()};
        thresholds = thresholder.get_thresholds();
        std::tie(minimal_number_of_minimizers, maximal_number_of_minimizers) = thresholder.get_minmax();
    }

    size_t bins{arguments.bin_path.size()};
    size_t technical_bins{seqan::hibf::next_multiple_of_64(bins)};
    assert(bins == technical_bins); // Todo: Important?

    constexpr bool profile = true;

    size_t const chunk_bits = std::min<size_t>(technical_bins, MAX_BUS_WIDTH);

    assert(MAX_BUS_WIDTH <= 512);

    auto process = [&]<size_t chunk_bits>()
    {
        min_ibf_fpga_oneapi<chunk_bits, profile> ibf(arguments.index_file,
                                                     minimal_number_of_minimizers,
                                                     maximal_number_of_minimizers,
                                                     std::move(thresholds),
                                                     arguments.buffer,
                                                     arguments.kernels);
        ibf.count(arguments.query_file, arguments.out_file);
    };

    switch (chunk_bits)
    {
        RAPTOR_FOR_EACH(RAPTOR_CASE_MACRO, FPGA_BINS)
    default:
        seqan::hibf::unreachable();
    }
}
