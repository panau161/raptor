#pragma once

#if defined(FPGA_EMULATOR) == defined(FPGA_HARDWARE)
#    error "Either FPGA_EMULATOR or FPGA_HARDWARE have to be defined."
#endif

#include <dlfcn.h>
#include <string>

using namespace std::string_literals;

#if __INTEL_LLVM_COMPILER < 20230000
#    include <CL/sycl.hpp>
#else
#    include <sycl/sycl.hpp>
#endif

#include <filesystem>

#include <cereal/archives/binary.hpp>
// #include <sdsl/bit_vectors.hpp>
#include <seqan3/contrib/sdsl-lite.hpp>

#include "min_ibf_fpga/backend_sycl/exception_handler.hpp"
#include <sycl/ext/intel/fpga_extensions.hpp>

template <size_t chunk_bits, bool profile = false>
class min_ibf_fpga_oneapi
{
    using HostSizeType = min_ibf_fpga::backend_sycl::HostSizeType;
    using Chunk = ac_int<chunk_bits, false>;

private:
    size_t const window_size{};
    size_t const kmer_size{};
    // The number of bins stored in the IBF (next multiple of 64 of `bins`).
    size_t technical_bins{};
    // The size of each bin in bits.
    size_t bin_size{};
    // The number of bits to shift the hash value before doing multiplicative hashing.
    size_t hash_shift{};
    seqan3::contrib::sdsl::bit_vector data{};

    size_t const minimalNumberOfMinimizers{};
    size_t const maximalNumberOfMinimizers{};
    sycl::queue utilitiesQueue;
    sycl::queue kernelQueue;
    std::vector<size_t> thresholds_size_t;
    size_t const bufferSizeBytes;
    size_t const numberOfKernelCopys;
    using timeUnit = std::chrono::duration<double, std::milli>;
    std::chrono::nanoseconds constructorDuration{};
    std::array<sycl::event, 2> setupEvents; // 0: transferThresholds, 1: transferIBF
    struct buffer_data
    {
        size_t numberOfQueries;
        std::vector<std::string> ids;

        char * queries_host;            // size: currentBufferSize
        HostSizeType * querySizes_host; // size: numberOfQueries
        Chunk * results_host;

        std::vector<sycl::event> kernelEvents;
    };
    std::array<buffer_data, 2> double_buffer;

    void setup_fpga()
    {
#ifdef FPGA_EMULATOR
        auto device_selector = sycl::ext::intel::fpga_emulator_selector_v;
#else
        auto device_selector = sycl::ext::intel::fpga_selector_v;
#endif
        if constexpr (profile)
            utilitiesQueue =
                sycl::queue(device_selector, fpga_tools::exception_handler, sycl::property::queue::enable_profiling());
        else
            utilitiesQueue = sycl::queue(device_selector, fpga_tools::exception_handler);

        sycl::device dev = utilitiesQueue.get_device();

        if (!dev.has(sycl::aspect::usm_device_allocations) /*dev.get_info<info::device::usm_device_allocations>()*/)
        {
            std::cerr << "ERROR: The selected device does not support USM device allocations\n";
            std::terminate();
        }
        if (!dev.has(sycl::aspect::usm_host_allocations) /*dev.get_info<info::device::usm_host_allocations>()*/)
        {
            std::cerr << "ERROR: The selected device does not support USM host allocations\n";
            std::terminate();
        }

        if constexpr (profile)
            kernelQueue =
                sycl::queue(device_selector, fpga_tools::exception_handler, sycl::property::queue::enable_profiling());
        else
            kernelQueue = sycl::queue(device_selector, fpga_tools::exception_handler);
    }

public:
    min_ibf_fpga_oneapi(uint8_t w,
                        uint8_t k,
                        size_t b,
                        cereal::BinaryInputArchive & archive,
                        size_t minimalNumberOfMinimizers,
                        size_t maximalNumberOfMinimizers,
                        std::vector<size_t> & thresholds,
                        uint8_t const bufferSizeMiB,
                        uint8_t const numberOfKernelCopys) :
        window_size{w},
        kmer_size{k},
        technical_bins{b},
        minimalNumberOfMinimizers{minimalNumberOfMinimizers},
        maximalNumberOfMinimizers{maximalNumberOfMinimizers},
        thresholds_size_t{thresholds},
        bufferSizeBytes(bufferSizeMiB * 1'048'576),
        numberOfKernelCopys{numberOfKernelCopys}
    {
        setup_fpga();
        std::chrono::steady_clock::time_point start;

        if constexpr (profile)
            start = std::chrono::steady_clock::now();

        size_t bin_words{};
        size_t hash_funs{};

        archive(bin_size);
        archive(hash_shift);
        archive(bin_words);
        archive(hash_funs);

        archive(data);

        if constexpr (profile)
        {
            std::chrono::steady_clock::time_point const end = std::chrono::steady_clock::now();
            constructorDuration = end - start;
        }
    }

    std::filesystem::path find_shared_library() const
    {
#if FPGA_HARDWARE
        std::string library_suffix = ".fpga.so";
#else
        std::string library_suffix = ".fpga_emu.so";
#endif

        std::ostringstream oss;
        oss << "libraptor_search_fpga_oneapi_lib_kernel_w" << window_size << "_k" << kmer_size << "_b" << technical_bins
            << "_kernels" << numberOfKernelCopys << library_suffix;
        std::string name = oss.str();

        std::filesystem::path library_path{"../src/fpga"};
        std::filesystem::path test_path{"../../../../../src/fpga"};

        if (std::filesystem::exists(library_path))
        {
            library_path = library_path / name;
        }
        else if (std::filesystem::exists(test_path))
        {
            library_path = test_path / name;
        }
        else
        {
            library_path = std::filesystem::current_path() / name;
        }

        if (!std::filesystem::exists(library_path))
        {
            std::cerr << "ERROR: Expected shared object " << library_path << " not present." << std::endl;
            std::terminate();
        }

        return library_path;
    }

    void count(std::filesystem::path const & query_path, std::filesystem::path const & output_path)
    {
        std::chrono::steady_clock::time_point countStart;
        std::chrono::steady_clock::time_point hostStart;

        if constexpr (profile)
            countStart = std::chrono::steady_clock::now();

        size_t const data_size_bytes = (data.bit_size() + 63) / 64 * 64 / 8;
        size_t const number_of_chunks = INTEGER_DIVISION_CEIL(data_size_bytes, sizeof(Chunk));
        Chunk const * ibfData_host = reinterpret_cast<Chunk *>(data.data());

        auto ibfData_device = sycl::malloc_device<Chunk>(number_of_chunks, utilitiesQueue);
        setupEvents[1] = utilitiesQueue.memcpy(ibfData_device, ibfData_host, number_of_chunks * sizeof(Chunk));

        if constexpr (profile)
        {
            printDuration("IBF I/O:\t", countStart, constructorDuration);
            hostStart = std::chrono::steady_clock::now();
        }

        // RunKernelType is a function pointer to a function that takes multiple arguments and returns void.
        using RunKernelType = void (*)(sycl::queue &,
                                       char const *,
                                       HostSizeType const *,
                                       HostSizeType const,
                                       Chunk const *,
                                       HostSizeType const,
                                       HostSizeType const,
                                       HostSizeType const,
                                       HostSizeType const,
                                       HostSizeType const *,
                                       Chunk *,
                                       std::vector<sycl::event> &);

        std::filesystem::path library_path = find_shared_library();
        void * handle = dlopen(library_path.c_str(), RTLD_NOW);

        if (!handle)
        {
            throw std::runtime_error{dlerror()};
        }

        dlerror(); // Clear any existing error

        std::string const symbol_name = "RunKernel";
        RunKernelType RunKernel = reinterpret_cast<RunKernelType>(dlsym(handle, symbol_name.c_str()));

        char * error_message = dlerror();

        if (error_message != NULL)
        {
            throw std::runtime_error{error_message};
        }

        static_assert(sizeof(HostSizeType) == sizeof(size_t));

        auto thresholds_device = sycl::malloc_device<HostSizeType>(thresholds_size_t.size(), utilitiesQueue);
        //setupEvents[0] = utilitiesQueue.copy(thresholds_device, thresholds_size_t.data(), thresholds_size_t.size()); // typed API does not seem to work
        setupEvents[0] = utilitiesQueue.memcpy(thresholds_device,
                                               thresholds_size_t.data(),
                                               thresholds_size_t.size() * sizeof(HostSizeType));

        std::ofstream outputStream(output_path, std::ios::out);
        std::string result_string{};

        for (buffer_data & state : double_buffer)
        {
            // TODO bufferSizeBytes is a way too high upper bound
            state.queries_host = sycl::malloc_host<char>(bufferSizeBytes, utilitiesQueue);
            state.querySizes_host = sycl::malloc_host<HostSizeType>(bufferSizeBytes, utilitiesQueue);
            state.results_host = sycl::malloc_host<Chunk>(bufferSizeBytes, utilitiesQueue);

            state.kernelEvents.reserve(numberOfKernelCopys * 2 + 2); // +2: Distributor, Collector
            state.numberOfQueries = 0;
        }

        auto const queueToFPGA = [&](struct buffer_data * state, size_t computeIteration)
        {
            if (computeIteration == 0)
            {
                utilitiesQueue.wait();
                if constexpr (profile)
                {
                    printDurationFromEvent("Thresh. Trans.:\t", setupEvents[0]);
                    printDurationFromEvent("IBF Transfer:\t", setupEvents[1]);
                }
            }

            std::chrono::steady_clock::time_point start;

            if constexpr (profile)
                start = std::chrono::steady_clock::now();

            RunKernel(kernelQueue,
                      state->queries_host,
                      state->querySizes_host,
                      state->numberOfQueries,
                      ibfData_device,
                      bin_size,
                      hash_shift,
                      minimalNumberOfMinimizers,
                      maximalNumberOfMinimizers,
                      thresholds_device,
                      state->results_host,
                      state->kernelEvents);

            if constexpr (profile)
                printDuration("Queue:\t\t", start);
        };

        auto const waitOnFPGA = [&](buffer_data * state)
        {
            std::chrono::steady_clock::time_point start;

            if constexpr (profile)
                start = std::chrono::steady_clock::now();

            for (sycl::event e : state->kernelEvents)
                e.wait();

            if constexpr (profile)
                printDuration("Wait:\t\t", start);
        };

        auto const outputResults = [&](buffer_data * state)
        {
            std::chrono::steady_clock::time_point start;

            if constexpr (profile)
                start = std::chrono::steady_clock::now();

            result_string.clear();

            size_t const elements_per_query = technical_bins / 64;
            size_t const chunks_per_query = INTEGER_DIVISION_CEIL(technical_bins, chunk_bits);
            size_t const elements_per_chunk = chunk_bits / 64;

            if (elements_per_chunk * chunks_per_query != elements_per_query)
                throw std::runtime_error("outputResults: elements_per_query/chunks_per_query mismatch");

            for (size_t queryIndex = 0; queryIndex < state->ids.size(); queryIndex++)
            {
                // TODO: Use string view?
                result_string += state->ids.at(queryIndex).substr(1, std::string::npos) + '\t';

                for (size_t chunkOffset = 0; chunkOffset < chunks_per_query; ++chunkOffset)
                {
                    Chunk & chunk = state->results_host[queryIndex * chunks_per_query + chunkOffset];

                    for (size_t elementOffset = 0; elementOffset < elements_per_chunk; ++elementOffset)
                    {
                        uint64_t const & element = ((uint64_t *)&chunk)[elementOffset];

                        if (!element)
                        {
                            continue;
                        }

                        for (size_t byteOffset = 0; byteOffset < 8; ++byteOffset)
                        {
                            uint8_t const & value = ((uint8_t *)&element)[byteOffset];

                            if (!value)
                            {
                                continue;
                            }

                            uint8_t mask = 1;

                            for (size_t bitOffset = 0; bitOffset < 8; ++bitOffset)
                            {
                                if (value & mask)
                                {
                                    result_string += std::to_string(chunkOffset * chunk_bits + elementOffset * 64
                                                                    + byteOffset * 8 + bitOffset)
                                                   + ",";
                                }
                                mask <<= 1;
                            }
                        }
                    }
                }
                result_string += '\n';
            }

            outputStream << result_string;

            if constexpr (profile)
                printDuration("Output:\t\t", start);

            if constexpr (profile)
            {
                std::stringstream profilingOutput;

                profilingOutput << "\n" << "Kernels:\t" << getRuntime(state->kernelEvents) << " ms\n";

                for (size_t kernelId = 0; kernelId < state->kernelEvents.size(); ++kernelId)
                    profilingOutput << "Kernel " << kernelId << ":\t" << getRuntime(state->kernelEvents[kernelId])
                                    << " ms\n";

                profilingOutput << "\n";

                std::clog << profilingOutput.str();
            }

            state->kernelEvents.clear();
        };

        std::ifstream inputStream(query_path, std::ios::in | std::ios::binary);

        std::string id;
        std::string query;

        size_t computeIteration = 0;
        size_t currentBufferSize = 0;

        buffer_data * currentBufferData = &double_buffer[0];

        while (std::getline(inputStream, id))
        //for (auto && [id, query] : fin)
        {
            std::getline(inputStream, query);

            if (currentBufferSize + query.size() > bufferSizeBytes)
            {
                if (currentBufferData->numberOfQueries == 0)
                    throw std::runtime_error("Buffer size to small");

                if constexpr (profile)
                    printDuration("Host:\t\t", hostStart);

                queueToFPGA(currentBufferData, computeIteration);

                currentBufferData = &double_buffer[++computeIteration % 2];

                if (computeIteration >= 2)
                {
                    waitOnFPGA(currentBufferData);
                    outputResults(currentBufferData); // could run async up to the next kernel start
                }

                if constexpr (profile)
                    hostStart = std::chrono::steady_clock::now();

                currentBufferData->ids.clear();
                currentBufferData->numberOfQueries = 0;

                currentBufferSize = 0;
            }

            currentBufferData->ids.emplace_back(std::move(id));

            currentBufferData->querySizes_host[currentBufferData->numberOfQueries] = query.size();

            // Copy query to queries
            std::memcpy(currentBufferData->queries_host + currentBufferSize, query.data(), query.size());

            currentBufferSize += query.size();
            currentBufferData->numberOfQueries++;

            // Discard two lines from input stream (delimiter and quality)
            for (size_t i = 0; i < 2; ++i)
                inputStream.ignore(std::numeric_limits<std::streamsize>::max(), inputStream.widen('\n'));
        }

        // Handle left over queries
        if (currentBufferData->numberOfQueries)
        {
            if constexpr (profile)
                printDuration("Host:\t\t", hostStart);

            queueToFPGA(currentBufferData, computeIteration);

            computeIteration++;
        }

        // Wait for the remaining ones to finish
        for (int i = std::min(computeIteration, 2ul); i > 0; i--)
        {
            currentBufferData = &double_buffer[(computeIteration + i) % 2];
            waitOnFPGA(currentBufferData);
            outputResults(currentBufferData);
        }

        sycl::free(ibfData_device, utilitiesQueue);
        sycl::free(thresholds_device, utilitiesQueue);

        for (buffer_data & state : double_buffer)
        {
            sycl::free(state.queries_host, utilitiesQueue);
            sycl::free(state.querySizes_host, utilitiesQueue);
            sycl::free(state.results_host, utilitiesQueue);
        }

        if constexpr (profile)
            printDuration("Count total:\t", countStart);
    }

private:
    static void printDuration(std::string const & label,
                              std::chrono::steady_clock::time_point const & start,
                              std::chrono::steady_clock::duration duration = std::chrono::steady_clock::duration())
    {
        auto const end = std::chrono::steady_clock::now();

        duration += end - start;
        auto const runtime = std::chrono::duration_cast<timeUnit>(duration).count();

        std::stringstream profilingOutput;

        profilingOutput << label << runtime << " ms\n";

        std::clog << profilingOutput.str();
    }

    static void printDurationFromEvent(std::string const & label, sycl::event const event)
    {
        uint64_t const start = event.get_profiling_info<sycl::info::event_profiling::command_start>();
        uint64_t const end = event.get_profiling_info<sycl::info::event_profiling::command_end>();

        uint64_t const duration_ns = end - start;
        float const duration_ms = static_cast<float>(duration_ns) / 1000000;

        std::stringstream profilingOutput;

        profilingOutput << label << duration_ms << " ms\n";

        std::clog << profilingOutput.str();
    }

    // Helper for SYCL profiling info

    static double getRuntime(std::vector<sycl::event> const events)
    {
        auto const comparator_start = [](sycl::event const & lhs, sycl::event const & rhs)
        {
            uint64_t lhsValue, rhsValue;

            lhsValue = lhs.get_profiling_info<sycl::info::event_profiling::command_start>();
            rhsValue = rhs.get_profiling_info<sycl::info::event_profiling::command_start>();

            return lhsValue < rhsValue;
        };

        auto const comparator_end = [](sycl::event const & lhs, sycl::event const & rhs)
        {
            uint64_t lhsValue, rhsValue;

            lhsValue = lhs.get_profiling_info<sycl::info::event_profiling::command_end>();
            rhsValue = rhs.get_profiling_info<sycl::info::event_profiling::command_end>();

            return lhsValue < rhsValue;
        };

        auto const startEvent = *std::min_element(events.begin(),
                                                  events.end(),
                                                  [&](auto const & lhs, auto const & rhs)
                                                  {
                                                      return comparator_start(lhs, rhs);
                                                  });
        auto const endEvent = *std::max_element(events.begin(),
                                                events.end(),
                                                [&](auto const & lhs, auto const & rhs)
                                                {
                                                    return comparator_end(lhs, rhs);
                                                });

        return getRuntime(startEvent, endEvent);
    };

    static double getRuntime(sycl::event const & event)
    {
        return getRuntime(event, event);
    }

    static double getRuntime(sycl::event const & startEvent, sycl::event const & endEvent)
    {
        uint64_t start, end;

        start = startEvent.get_profiling_info<sycl::info::event_profiling::command_start>();
        end = endEvent.get_profiling_info<sycl::info::event_profiling::command_end>();

        std::chrono::nanoseconds const duration{end - start};

        return std::chrono::duration_cast<timeUnit>(duration).count();
    }
};
