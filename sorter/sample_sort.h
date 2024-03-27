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

#include "absl/container/btree_map.h"
#include "parlay/primitives.h"

#include "config.h"
#include "utils/file_utils.h"
#include "utils/unordered_file_reader.h"
#include "utils/ordered_file_writer.h"

template<typename T, typename Comparator>
class SampleSort {
private:
    static std::vector<T> GetSamples(std::vector<FileInfo> &file_list, size_t num_samples) {
        // num_samples is assumed to be < 10000, so parallelism shouldn't be necessary
        size_t size_prefix_sum[file_list.size()];
        for (size_t i = 0; i < file_list.size(); i++) {
            auto prev = i == 0 ? 0 : size_prefix_sum[i-1];
            size_prefix_sum[i] = file_list[i].true_size + prev;
        }
        size_t total_size = size_prefix_sum[file_list.size() - 1];
        size_t n = total_size / sizeof(T);
        // choose num_samples from n elements
        // use rejection sampling; the Vitter algorithms mentioned in the link are viable alternatives
        // (https://cs.stackexchange.com/questions/104930/efficient-n-choose-k-random-sampling)
        if (num_samples > n) {
            [[unlikely]]
            LOG(ERROR) << "Sample size is " << num_samples << " but we only have " << n << " elements";
        }
        std::set<size_t> sample_indices;
        std::random_device rd;
        std::mt19937_64 rng(rd());
        std::uniform_int_distribution<size_t> dis(0, n);
        for (size_t i = 0; i < num_samples; i++) {
            while (true) {
                size_t selected = dis(rng);
                if (!sample_indices.count(selected)) {
                    sample_indices.insert(selected);
                    break;
                }
            }
        }
        std::vector<size_t> sample_indices_list(sample_indices.begin(), sample_indices.end());
        std::vector<T> result;
        std::mutex result_lock;
        parlay::parallel_for(0, num_samples, [&](size_t i) {
            auto target_byte = sample_indices_list[i] * sizeof(T);
            auto file_num = std::upper_bound(size_prefix_sum, size_prefix_sum + file_list.size(), target_byte) - size_prefix_sum;
            auto file_offset = target_byte - (file_num == 0 ? 0 : size_prefix_sum[file_num - 1]);
            // reads must be aligned in O_DIRECT; compute the starting byte
            auto file_read_base = file_offset / O_DIRECT_MULTIPLE * O_DIRECT_MULTIPLE;
            uint8_t buffer[O_DIRECT_MULTIPLE];
            // FIXME: what if this read crosses block boundaries? only an issue if the size of T is not a power of 2
            if (file_read_base >= file_list[file_num].file_size) {
                [[unlikely]]
                LOG(ERROR) << "Read size exceeds file size. \n"
                    << "File size: " << file_list[file_num].file_size << "\n"
                    << "Read base: " << file_read_base;
                exit(0);
            }
            ReadFileOnce(file_list[file_num].file_name, buffer, file_read_base);
            std::lock_guard<std::mutex> lock(result_lock);
            result.push_back(*(T*)(buffer + (file_offset - file_read_base)));
        });
        return result;
    }

    void AssignToBucketThread(parlay::sequence<T> samples, size_t flush_threshold, Comparator comp) {
        // reads from the reader and put result into a thread-local buffer; send to intermediate_writer when buffer is full
        size_t num_buckets = samples.size() + 1;
        size_t buffer_size = flush_threshold / sizeof(T);
        T* buckets[num_buckets];
        unsigned int buffer_index[num_buckets];
        for (size_t i = 0; i < num_buckets; i++) {
            buckets[i] = (T*)malloc(buffer_size * sizeof(T));
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
                buckets[bucket_index][buffer_index[bucket_index]++] = data[i];
                if (buffer_index[bucket_index] == buffer_size) {
                    buffer_index[bucket_index] = 0;
                    intermediate_writer.Write(bucket_index, buckets[bucket_index], buffer_size);
                    buckets[bucket_index] = (T*)malloc(buffer_size * sizeof(T));
                }
            }
        }
        for (size_t i = 0; i < num_buckets; i++) {
            intermediate_writer.Write(i, buckets[i], buffer_index[i]);
        }
    }

    FileInfo SortBucket(const FileInfo& file_info, std::string target_file, Comparator comparator) {
        // use parlay's sorting utility to sort this bucket
        T *buffer = (T*)malloc(file_info.file_size);
        int fd = open(file_info.file_name.c_str(), O_RDONLY | O_DIRECT);
        SYSCALL(fd);
        SYSCALL(read(fd, buffer, file_info.file_size));
        close(fd);
        size_t n = file_info.true_size / sizeof(T);
        parlay::sequence<T> seq(buffer, buffer + n);
        parlay::sort_inplace(seq, comparator);
        for (size_t i = 0; i < n; i++) {
            buffer[i] = seq[i];
        }
        fd = open(target_file.c_str(), O_WRONLY | O_DIRECT | O_CREAT, 0744);
        SYSCALL(fd);
        SYSCALL(write(fd, buffer, file_info.file_size));
        close(fd);
        free(buffer);
        return {std::move(target_file), file_info.true_size, file_info.file_size};
    }

    static size_t GetSampleSize(const std::vector<FileInfo> &input_files) {
        // FIXME: considerations for sample size
        //   (1) samples should ideally fit in L1 cache for maximal binary search efficiency
        //   (2) each bucket should be small enough to fit in main memory; ideally they should be small each that we
        //       can process buckets concurrently to overlap IO and computation
        size_t file_size = 0;
        for (const auto &f : input_files) {
            file_size += f.true_size;
        }
        // FIXME: assuming no bucket is skewed to the point where it is 3 times the average size
        size_t min_sample_size = std::max(1UL, 3 * file_size / MAIN_MEMORY_SIZE);
        // max sample size cannot exceed the number of elements; it should also not result in very tiny files
        size_t max_sample_size = std::max(1UL, std::min(file_size / sizeof(T), file_size / O_DIRECT_MULTIPLE));
        // FIXME: need more stuff here; 1024 is a temporary value
        return std::max(std::min(1024UL, max_sample_size), min_sample_size);
    }

    UnorderedFileReader<T> reader;
    OrderedFileWriter<T> intermediate_writer;

public:

    std::vector<FileInfo> Sort(std::vector<FileInfo> &input_files,
                               const std::string& result_prefix,
                               Comparator comp) {
        GetFileInfo(input_files);
        reader.PrepFiles(input_files);
        reader.Start();
        size_t num_samples = GetSampleSize(input_files);
        size_t flush_threshold = 4 << 20;
        // FIXME: change this file name to a different one (possibly randomized?)
        intermediate_writer.Initialize("spfx_", num_samples + 1, 1 << 20);
        auto samples = GetSamples(input_files, num_samples);
        const auto sorted_pivots = parlay::sort(samples, comp);
        parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
            AssignToBucketThread(sorted_pivots, flush_threshold, comp);
        });
        // retrieve buckets from intermediate_writer
        auto bucket_list = intermediate_writer.ReapResult();
        std::mutex bucket_list_lock;
        std::vector<FileInfo> result_list(num_samples + 1);
        // FIXME: should allow as much parallelism as internal memory allows; need to calculate memory budget
        //   also, this can get tricky since using a conditional variable to guard against memory overflow
        //   may result in suboptimal performance
        parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
            FileInfo file_info;
            size_t bucket_number;
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(bucket_list_lock);
                    if (bucket_list.empty()) {
                        return;
                    }
                    file_info = bucket_list.back();
                    bucket_list.pop_back();
                    bucket_number = bucket_list.size();
                }
                auto result_name = GetFileName(result_prefix, bucket_number);
                SortBucket(file_info, result_name, comp);
                result_list[bucket_number].file_name = result_name;
                result_list[bucket_number].file_size = file_info.file_size;
                result_list[bucket_number].true_size = file_info.true_size;
            }
        });

        return result_list;
    }
};

#endif //SORTING_SAMPLE_SORT_H
