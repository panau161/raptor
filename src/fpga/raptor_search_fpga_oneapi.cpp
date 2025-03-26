#include <cmath>
#include <fstream>
#include <vector>

#include "min_ibf_fpga/backend_sycl/shared.hpp"

// #include "compute_simple_model_fpga.hpp"
#include <hibf/misc/unreachable.hpp>

#include <raptor/argument_parsing/search_arguments.hpp>
#include <raptor/fpga/min_ibf_fpga_oneapi.hpp>
#include <raptor/threshold/precompute_threshold.hpp>

void raptor_search_fpga_oneapi(raptor::search_fpga_arguments const & arguments)
{
    size_t const kmers_per_window = arguments.window_size - arguments.shape_size + 1;
    size_t const kmers_per_pattern = arguments.query_length - arguments.shape_size + 1;
    size_t const minimal_number_of_minimizers = kmers_per_pattern / kmers_per_window;
    size_t const maximal_number_of_minimizers = arguments.query_length - arguments.window_size + 1;

    std::vector<size_t> precomp_thresholds = precompute_threshold(arguments.make_threshold_parameters());

    std::ifstream archiveStream{arguments.index_file, std::ios::binary};
    cereal::BinaryInputArchive archive{archiveStream};

    size_t bins{};
    size_t technical_bins{};

    archive(bins);
    archive(technical_bins);

    assert(bins == technical_bins); // Todo: Important?

    constexpr bool profile = true;

    size_t const chunk_bits = std::min<size_t>(technical_bins, MAX_BUS_WIDTH);

    assert(MAX_BUS_WIDTH <= 512);

    auto process = [&]<size_t chunk_bits>()
    {
        min_ibf_fpga_oneapi<chunk_bits, profile> ibf(arguments.window_size,
                                                     arguments.shape_size,
                                                     technical_bins,
                                                     archive,
                                                     minimal_number_of_minimizers,
                                                     maximal_number_of_minimizers,
                                                     precomp_thresholds,
                                                     arguments.buffer,
                                                     arguments.kernels);
        ibf.count(arguments.query_file, arguments.out_file);
    };

    // Generate all possible host code specialisations
    switch (chunk_bits)
    {
    case 64:
        process.template operator()<64>();
        break;
    // case 128:
    //     process.template operator()<128>();
    //     break;
    // case 256:
    //     process.template operator()<256>();
    //     break;
    // case 512:
    //     process.template operator()<512>();
    //     break;
    default:
        seqan::hibf::unreachable();
        // Todo: Should be checked when parsing arguments.
        // std::cerr << "[Error] Unsupported number of bins." << std::endl;
        // exit(EXIT_FAILURE);
    }
}
