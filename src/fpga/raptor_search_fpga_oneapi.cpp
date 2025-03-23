#include <cmath>
#include <fstream>
#include <vector>

#include "min_ibf_fpga/backend_sycl/shared.hpp"

// #include "compute_simple_model_fpga.hpp"
#include <raptor/argument_parsing/search_arguments.hpp>
#include <raptor/fpga/min_ibf_fpga_oneapi.hpp>
#include <raptor/threshold/precompute_threshold.hpp>

void raptor_search_fpga_oneapi(raptor::search_fpga_arguments const & arguments)
{
    size_t const kmers_per_window = arguments.window_size - arguments.shape_size + 1;
    size_t const kmers_per_pattern = arguments.query_length - arguments.shape_size + 1;
    size_t const minimal_number_of_minimizers =
        kmers_per_window == 1 ? kmers_per_pattern
                              : std::ceil(kmers_per_pattern / static_cast<double>(kmers_per_window));
    size_t const maximal_number_of_minimizers = arguments.query_length - arguments.window_size + 1;

    std::vector<size_t> precomp_thresholds = precompute_threshold(arguments.make_threshold_parameters());

    std::ifstream archiveStream{arguments.index_file, std::ios::binary};
    cereal::BinaryInputArchive archive{archiveStream};

    size_t bins{};
    size_t technical_bins{};

    archive(bins);
    archive(technical_bins);

    assert(bins == technical_bins);

    bool const profile = true;

    size_t const chunk_bits = technical_bins > MAX_BUS_WIDTH ? MAX_BUS_WIDTH : technical_bins;

    assert(MAX_BUS_WIDTH <= 512);

    // Generate all possible host code specialisations
    if (chunk_bits == 64)
    {
        min_ibf_fpga_oneapi<64, profile> ibf(arguments.window_size,
                                             arguments.shape_size,
                                             technical_bins,
                                             archive,
                                             minimal_number_of_minimizers,
                                             maximal_number_of_minimizers,
                                             precomp_thresholds,
                                             arguments.buffer,
                                             arguments.kernels);
        ibf.count(arguments.query_file, arguments.out_file);
    }
    else if (chunk_bits == 128)
    {
        min_ibf_fpga_oneapi<128, profile> ibf(arguments.window_size,
                                              arguments.shape_size,
                                              technical_bins,
                                              archive,
                                              minimal_number_of_minimizers,
                                              maximal_number_of_minimizers,
                                              precomp_thresholds,
                                              arguments.buffer,
                                              arguments.kernels);
        ibf.count(arguments.query_file, arguments.out_file);
    }
    else if (chunk_bits == 256)
    {
        min_ibf_fpga_oneapi<256, profile> ibf(arguments.window_size,
                                              arguments.shape_size,
                                              technical_bins,
                                              archive,
                                              minimal_number_of_minimizers,
                                              maximal_number_of_minimizers,
                                              precomp_thresholds,
                                              arguments.buffer,
                                              arguments.kernels);
        ibf.count(arguments.query_file, arguments.out_file);
    }
    else if (chunk_bits == 512)
    {
        min_ibf_fpga_oneapi<512, profile> ibf(arguments.window_size,
                                              arguments.shape_size,
                                              technical_bins,
                                              archive,
                                              minimal_number_of_minimizers,
                                              maximal_number_of_minimizers,
                                              precomp_thresholds,
                                              arguments.buffer,
                                              arguments.kernels);
        ibf.count(arguments.query_file, arguments.out_file);
    }
    else
    {
        std::cerr << "[Error] Unsupported number of bins." << std::endl;
        exit(EXIT_FAILURE);
    }
}
