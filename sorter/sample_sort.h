//
// Created by peter on 3/18/24.
//

#ifndef SORTING_SAMPLE_SORT_H
#define SORTING_SAMPLE_SORT_H

#include <utility>
#include <vector>
#include <string>
#include <functional>

#include "absl/container/btree_map.h"
#include "parlay/primitives.h"

#include "config.h"
#include "utils/file_utils.h"
#include "utils/unordered_file_reader.h"
#include "utils/ordered_file_writer.h"

template<typename T, typename Comparator>
class SampleSort {
private:
    std::vector<T> GetSamples(std::vector<std::string> &file_list, size_t num_samples) {
        // FIXME: what assumptions are we making about the input file list? do we know their size in the fs? do we
        //   know their true size?
        return {};
    }

    void AssignToBucketThread(parlay::sequence<T> samples, size_t flush_threshold, Comparator comp) {
        // reads from the reader and put result into a thread-local buffer; send to writer when buffer is full
        size_t num_buckets = samples.size() + 1;
        size_t buffer_count = flush_threshold / sizeof(T);
        T* buffers[num_buckets];
        unsigned int buffer_index[num_buckets];
        for (size_t i = 0; i < num_buckets; i++) {
            buffers[i] = malloc(buffer_count * sizeof(T));
            buffer_index[i] = 0;
        }
        while (true) {
            auto [data, size] = reader.Poll();
            if (data == nullptr) {
                break;
            }
            for (size_t i = 0; i < size; i++) {
                auto iter = std::upper_bound(samples.begin(), samples.end(), data[i], comp);
                size_t bucket_index = std::distance(samples.begin(), iter);
                buffers[bucket_index][buffer_index[bucket_index]++] = data[i];
                if (buffer_index[bucket_index] == buffer_count) {
                    buffer_index[bucket_index] = 0;
                    writer.Write(bucket_index, buffers[bucket_index], buffer_count);
                    buffers[buffer_index] = malloc(buffer_count * sizeof(T));
                }
            }
        }
        for (size_t i = 0; i < num_buckets; i++) {
            writer.Write(i, buffers[i], buffer_index[i]);
        }
    }

    FileInfo SortBucket(const FileInfo& file_info, std::string target_file, Comparator comparator) {
        // use parlay's sorting utility to sort this bucket
        T *buffer = malloc(file_info.file_size);
        int fd = open(file_info.file_name.c_str(), O_RDONLY | O_DIRECT);
        read(fd, buffer, file_info.file_size);
        close(fd);
        parlay::sort_inplace(buffer, comparator);
        fd = open(target_file.c_str(), O_WRONLY | O_DIRECT | O_CREAT);
        write(fd, buffer, file_info.file_size);
        close(fd);
        free(buffer);
        return {std::move(target_file), file_info.true_size, file_info.file_size};
    }

    UnorderedFileReader<T> reader;
    OrderedFileWriter<T> writer;

public:

    std::vector<FileInfo> Sort(std::vector<FileInfo> &input_files, const std::string& result_prefix, Comparator comp) {
        // FIXME: considerations for sample size
        //   (1) samples should ideally fit in L1 cache for maximal binary search efficiency
        //   (2) each bucket should be small enough to fit in main memory; ideally they should be small each that we
        //       can process buckets concurrently to overlap IO and computation
        reader.PrepFiles(input_files);
        size_t num_samples = 1024;
        size_t flush_threshold = 4 << 20;
        writer.Initialize(result_prefix, num_samples + 1, 1 << 20);
        auto samples = GetSamples(input_files, num_samples);
        const auto sorted_pivots = parlay::sort(samples, comp);
        parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
            AssignToBucketThread(sorted_pivots, flush_threshold, comp);
        });
        // retrieve buckets from writer
        auto bucket_list = writer.ReapResult();
        std::mutex bucket_list_lock;
        // FIXME: should allow as much parallelism as internal memory allows; need to calculate memory budget
        parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
            FileInfo file_info;
            size_t bucket_number;
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(bucket_list_lock);
                    if (bucket_list.empty()) {
                        return;
                    }
                    file_info = bucket_list.pop_back();
                    bucket_number = bucket_list.size();
                }
                SortBucket(file_info, GetFileName(result_prefix, bucket_number), comp);
            }
        });

        return {};
    }
};

#endif //SORTING_SAMPLE_SORT_H
