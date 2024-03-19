//
// Created by peter on 3/18/24.
//

#ifndef SORTING_SAMPLE_SORT_H
#define SORTING_SAMPLE_SORT_H

#include <vector>
#include <string>
#include <functional>

#include "absl/container/btree_map.h"
#include "parlay/primitives.h"

#include "config.h"
#include "utils/unordered_file_reader.h"
#include "utils/ordered_file_writer.h"

template<typename T, typename Comparator>
class SampleSort {
private:
    std::vector<T> GetSamples(std::vector<std::string> &file_list, size_t num_samples) {
        return {};
    }

    void SampleSortThread(parlay::sequence<T> buckets, size_t flush_threshold) {
        // reads from the reader and put result into a thread-local buffer; send to writer when buffer is full
    }

    std::vector<std::string> SortBucket(std::vector<std::string> &file_list) {
        // use parlay's sorting utility to sort this bucket
        return {};
    }

    UnorderedFileReader<T> reader;
    OrderedFileWriter<T> writer;

public:
    std::vector<std::string> Sort(std::vector<std::string> &file_names, Comparator comp) {
        size_t num_samples = 1024;
        size_t flush_threshold = 4 << 20;
        auto samples = GetSamples(file_names, num_samples);
        const auto sorted_pivots = parlay::sort(samples, comp);
        parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
            SampleSortThread(sorted_pivots, flush_threshold);
        });
        // retrieve buckets from writer
        
    }
};

#endif //SORTING_SAMPLE_SORT_H
