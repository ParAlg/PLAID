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
#include "parlay/internal/get_time.h"
#include "parlay/alloc.h"

#include "configs.h"
#include "utils/file_utils.h"
#include "utils/unordered_file_reader.h"
#include "utils/ordered_file_writer.h"
#include "utils/random_read.h"

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
        for (const auto &info : file_list) {
            total_size += info.true_size;
        }
        // n is the number of elements in all files
        size_t n = total_size / sizeof(T);
        DLOG(INFO) << "Found " << n << " elements in input files";
        if (num_pivots > n) {
            [[unlikely]]
            LOG(ERROR) << "Sample size is " << num_pivots << " but we only have " << n << " elements";
        }
        size_t oversample_size = std::min(n, num_pivots * num_pivots + 2 * num_pivots);
        parlay::random_generator generator;
        std::uniform_int_distribution<size_t> dis(0, n - 1);
        auto samples = parlay::sort(RandomBatchRead<T>(file_list, parlay::map(parlay::iota(oversample_size), [&](size_t i) {
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

    void VerifyBucket(size_t start, size_t end, T *ptr, size_t size) {
        for (size_t i = 0; i < size; i++) {
            if (ptr[i] < start || ptr[i] > end) {
                LOG(ERROR) << ptr[i] << " is not within range from " << start << " to " << end;
            }
        }
    }

    struct BucketData {
        char data[SAMPLE_SORT_BUCKET_SIZE];
    };
    using bucket_allocator = parlay::type_allocator<BucketData>;

    /**
     * Keep polling from the reader for data and then assign items into buckets according to how they compare
     * with samples. Buckets are flushed to the writer whenever they're full.
     *
     * The function is meant to be run as a thread and exits when the reader returns nullptr (no more input available)
     *
     * @tparam Comparator
     * @param samples
     * @param flush_threshold The number of bytes in each bucket. This is the threshold at which a bucket gets sent
     * to the writer. It may or may not be written to disk at the writer's discretion.
     * @param comp
     */
    template<typename Comparator>
    void AssignToBucket(const parlay::sequence<T> &samples, Comparator comp) {
        // reads from the reader and put result into a thread-local buffer; send to intermediate_writer when buffer is full
        size_t num_buckets = samples.size() + 1;
        size_t buffer_size = SAMPLE_SORT_BUCKET_SIZE / sizeof(T);
        // each bucket stores a pointer to an array, which will hold temporary values in that bucket
        T *buckets[num_buckets];
        unsigned int buffer_index[num_buckets];
        for (size_t i = 0; i < num_buckets; i++) {
//            // may need posix_memalign since writev under O_DIRECT expects pointers to be aligned as well
//            posix_memalign((void**)&buckets[i], O_DIRECT_MULTIPLE, buffer_size * sizeof(T));
            buckets[i] = (T*)bucket_allocator::alloc();
            buffer_index[i] = 0;
        }
        while (true) {
            auto [data, size] = reader.Poll();
            if (data == nullptr) {
                break;
            }
            // TODO: sort chunk in memory and then interleave samples with it
            for (size_t i = 0; i < size; i++) {
                // use binary search to find which bucket it belongs to
                auto iter = std::upper_bound(samples.begin(), samples.end(), data[i], comp);
                size_t bucket_index = std::distance(samples.begin(), iter);
                // move data to bucket
                buckets[bucket_index][buffer_index[bucket_index]++] = data[i];
                // flush if bucket is full
                if (buffer_index[bucket_index] == buffer_size) {
                    buffer_index[bucket_index] = 0;
                    intermediate_writer.Write(bucket_index, buckets[bucket_index], buffer_size);
                    buckets[bucket_index] = (T*)bucket_allocator::alloc();
                }
            }
            free(data);
        }
        // cleanup partially full buckets
        for (size_t i = 0; i < num_buckets; i++) {
            // if bucket is empty, free and do nothing else; otherwise send to writer
            if (buffer_index[i] == 0) {
                bucket_allocator::free((BucketData*)buckets[i]);
            } else {
                intermediate_writer.Write(i, buckets[i], buffer_index[i]);
            }
        }
    }

    /**
     * Sort a file's content in internal memory
     * @tparam Comparator
     * @param file_info
     * @param target_file File to which the result of the sorting algorithm will be written
     * @param comparator
     * @return Information of the resulting file
     */
    template<typename Comparator>
    FileInfo SortBucket(const FileInfo &file_info, std::string target_file, Comparator comparator) {
        // use parlay's sorting utility to sort this bucket
        T *buffer = (T *) ReadEntireFile(file_info.file_name, file_info.file_size);
        size_t n = file_info.true_size / sizeof(T);
        auto seq = parlay::make_slice(buffer, buffer + n);
        parlay::sort_inplace(seq, comparator);
        int fd = open(target_file.c_str(), O_WRONLY | O_DIRECT | O_CREAT, 0744);
        SYSCALL(fd);
        SYSCALL(write(fd, buffer, file_info.file_size));
        close(fd);
        free(buffer);
        return {std::move(target_file), file_info.true_size, file_info.file_size};
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
        size_t min_sample_size = std::max(1UL, 3 * file_size / MAIN_MEMORY_SIZE);
        // max sample size cannot exceed the number of elements; it should also not result in very tiny files
        size_t max_sample_size = std::max(1UL, std::min(file_size / sizeof(T), file_size / O_DIRECT_MULTIPLE));
        // FIXME: need more stuff here; ~128MB per bucket is temporary
        return std::max(std::min(file_size / (1UL << 27), max_sample_size), min_sample_size);
    }

    // reader for the input files
    UnorderedFileReader<T> reader;
    // writer to handle all the buckets created in phase 1 of sample sort
    OrderedFileWriter<T, SAMPLE_SORT_BUCKET_SIZE> intermediate_writer;

public:

    template<typename Comparator>
    std::vector<FileInfo> Sort(std::vector<FileInfo> &input_files,
                               const std::string &result_prefix,
                               Comparator comp) {
        DLOG(INFO) << "Performing sample sort with " << input_files.size() << " files";
        parlay::internal::timer timer("Sample sort", true);
        GetFileInfo(input_files);
        reader.PrepFiles(input_files);
        reader.Start();
        size_t num_samples = GetSampleSize(input_files);
        // FIXME: change this file name to a different one (possibly randomized?)
        intermediate_writer.Initialize("spfx_", num_samples + 1, 1 << 20);
        timer.next("Before sampling " + std::to_string(num_samples) + " samples with bucket size ");
        auto samples = GetPivots(input_files, num_samples);
        const auto sorted_pivots = parlay::sort(samples, comp);
        timer.next("After sampling and before assign to bucket");
        parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
            AssignToBucket(sorted_pivots, comp);
        }, 1);
        // retrieve buckets from intermediate_writer
        auto bucket_list = intermediate_writer.ReapResult();
        timer.next("After assign to bucket and before phase 2");
        std::mutex bucket_list_lock;
        std::vector<FileInfo> result_list(num_samples + 1);
        // FIXME: should allow as much parallelism as internal memory allows; need to calculate memory budget
        //   also, this can get tricky since using a conditional variable to guard against memory overflow
        //   may result in suboptimal performance
        parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
            // each thread gets a bucket and sorts the content of that bucket in internal memory
            FileInfo file_info;
            size_t bucket_number;
            while (true) {
                // get a bucket
                {
                    std::lock_guard<std::mutex> lock(bucket_list_lock);
                    if (bucket_list.empty()) {
                        return;
                    }
                    file_info = bucket_list.back();
                    bucket_list.pop_back();
                    bucket_number = bucket_list.size();
                }
                // sort this bucket in internal memory
                auto result_name = GetFileName(result_prefix, bucket_number);
                SortBucket(file_info, result_name, comp);
                result_list[bucket_number].file_name = result_name;
                result_list[bucket_number].file_size = file_info.file_size;
                result_list[bucket_number].true_size = file_info.true_size;
            }
        }, 1);
        timer.next("Sorting complete");
        timer.stop();
        return result_list;
    }
};

#endif //SORTING_SAMPLE_SORT_H
