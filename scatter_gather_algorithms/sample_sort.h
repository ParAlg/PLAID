//
// Created by peter on 3/18/24.
//

#ifndef SORTING_SAMPLE_SORT_H
#define SORTING_SAMPLE_SORT_H

#include <utility>
#include <vector>
#include <string>
#include <functional>
#include <set>

#include "parlay/primitives.h"
#include "parlay/internal/get_time.h"

#include "configs.h"
#include "utils/file_utils.h"
#include "utils/random_read.h"

#include "scatter_gather.h"

/**
 * Perform external memory sample sort
 *
 * @tparam T The type to be sorted
 */
template<typename T>
class SampleSort {
private:
    /**
     * Obtain random samples from a list of files
     *
     * @param file_list
     * @param num_pivots
     * @return A vector of <code>num_pivots</code> samples
     */
    static std::vector<T> GetPivots(const std::vector<FileInfo> &file_list, size_t num_pivots) {
        // num_pivots is assumed to be < 1024, so parallelism shouldn't be necessary
        size_t total_size = 0;
        for (const auto &info: file_list) {
            total_size += info.true_size;
        }
        // n is the number of elements in all files
        size_t n = total_size / sizeof(T);
        DLOG(INFO) << "Found " << n << " elements in input files";
        if (num_pivots > n) {
            [[unlikely]]
            LOG(ERROR) << "Sample size is " << num_pivots << " but we only have " << n << " elements";
        }
        // too many samples means the buckets are distributed too evenly; this will affect performance later on
        size_t oversample_size = std::min(n, num_pivots * (size_t) std::sqrt(
            num_pivots)); // std::min(n, num_pivots * num_pivots + 2 * num_pivots);
        parlay::random_generator generator;
        std::uniform_int_distribution<size_t> dis(0, n - 1);
        auto samples = parlay::sort(
            RandomBatchRead<T>(file_list, parlay::map(parlay::iota(oversample_size), [&](size_t i) {
                auto gen = generator[i];
                return dis(gen);
            })));
        std::vector<T> result;
        size_t remaining_pivots = num_pivots;
        size_t i = 0;
        while (remaining_pivots > 0) {
            i += std::max(1UL, (oversample_size - i - remaining_pivots) / (remaining_pivots + 1));
            result.push_back(samples[i]);
            // samples[i] is taken, so the first available sample is the next one
            i++;
            remaining_pivots--;
        }

        return result;
    }

    /**
     * Compute a sensible sample size based on information about the files that are about to be sorted.
     * @param input_files List of file to be sorted
     * @return An ideal sample size
     */
    static size_t GetSampleSize(const std::vector<FileInfo> &input_files) {
        // FIXME: considerations for sample size
        //   (1) samples should ideally fit in L1 cache for maximal binary search efficiency
        //   (2) each bucket should be small enough to fit in main memory; ideally they should be small each that we
        //       can process buckets concurrently to overlap IO and computation
        size_t file_size = 0;
        for (const auto &f: input_files) {
            file_size += f.true_size;
        }
        // FIXME: assuming no bucket is skewed to the point where it is 3 times the average size
        size_t min_sample_size = std::max(1UL, 4 * THREAD_COUNT * file_size / MAIN_MEMORY_SIZE);
        // max sample size cannot exceed the number of elements; it should also not result in very tiny files
        size_t max_sample_size = std::max(1UL, std::min(file_size / sizeof(T), file_size / O_DIRECT_MULTIPLE));
        // FIXME: need more stuff here; ~128MB per bucket is temporary
        return std::max(std::min(file_size / (1UL << 27), max_sample_size), min_sample_size);
    }

public:

    template<typename Comparator>
    std::vector<FileInfo> Sort(std::vector<FileInfo> &input_files,
                               const std::string &result_prefix,
                               const Comparator comp) {
        parlay::internal::timer timer("Sample sort internal", true);
        GetFileInfo(input_files);
        size_t num_samples = GetSampleSize(input_files);
        const auto pivots = parlay::sort(GetPivots(input_files, num_samples), comp);
        ScatterGather<T> scatter_gather;
        const auto simple_assigner = [&](const T &t) {
            return std::distance(pivots.begin(),
                                 std::upper_bound(pivots.begin(), pivots.end(),
                                                  t, comp));
        };
        const auto simple_processor = [&](T **buffer, size_t n) {
            T *ptr = *buffer;
            auto seq = parlay::make_slice(ptr, ptr + n);
            parlay::sort_inplace(seq, comp);
        };
        auto results = scatter_gather.Run(input_files, result_prefix, num_samples + 1,
                                          simple_assigner,
                                          simple_processor);
        timer.next("Sorting complete");
        timer.stop();
        return {results.begin(), results.end()};
    }
};

#endif //SORTING_SAMPLE_SORT_H
