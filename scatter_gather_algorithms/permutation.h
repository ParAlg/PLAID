//
// Created by peter on 7/31/24.
//

#ifndef SORTING_PERMUTATION_H
#define SORTING_PERMUTATION_H

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
     * Compute a sensible sample size based on information about the files that are about to be sorted.
     * @param input_files List of file to be sorted
     * @return An ideal sample size
     */
    static size_t GetBucketSize(const std::vector<FileInfo> &input_files) {
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

    std::vector<FileInfo> Permute(std::vector<FileInfo> &input_files,
                               const std::string &result_prefix) {
        parlay::internal::timer timer("Sample sort internal", true);
        GetFileInfo(input_files);
        size_t num_buckets = GetBucketSize(input_files);
        ScatterGather<T> scatter_gather;
        parlay::random_generator gen;
        const auto simple_assigner = [&](const T &t) {
            // TODO: use thread-local RNG?
            //  use t as random index, but this assumes that T is a numeric type
        };
        const auto simple_processor = [&](T **buffer, size_t n) {
            T *ptr = *buffer;
            parlay::sequence seq = parlay::make_slice(ptr, ptr + n);
            parlay::random_shuffle(seq);
        };
        auto results = scatter_gather.Run(input_files, result_prefix, num_buckets + 1,
                                          simple_assigner,
                                          simple_processor);
        timer.next("Sorting complete");
        timer.stop();
        return {results.begin(), results.end()};
    }
};

#endif //SORTING_SAMPLE_SORT_H


#endif //SORTING_PERMUTATION_H
