//
// Created by peter on 7/31/24.
//

#ifndef SORTING_SCATTER_GATHER_H
#define SORTING_SCATTER_GATHER_H

#include <utility>
#include <vector>
#include <string>
#include <functional>

#include "parlay/primitives.h"
#include "parlay/internal/get_time.h"
#include "parlay/alloc.h"

#include "configs.h"
#include "utils/file_utils.h"
#include "utils/unordered_file_reader.h"
#include "utils/ordered_file_writer.h"

/**
 * Perform external memory sample sort
 *
 * @tparam T The type to be sorted
 */
template<typename T>
class ScatterGather {
private:

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
    void AssignToBucket(size_t num_buckets, const std::function<size_t(const T&)> assigner) {
        // reads from the reader and put result into a thread-local buffer; send to intermediate_writer when buffer is full
        size_t buffer_size = SAMPLE_SORT_BUCKET_SIZE / sizeof(T);
        // each bucket stores a pointer to an array, which will hold temporary values in that bucket
        T *buckets[num_buckets];
        unsigned int buffer_index[num_buckets];
        for (size_t i = 0; i < num_buckets; i++) {
//            // may need posix_memalign since writev under O_DIRECT expects pointers to be aligned as well
//            posix_memalign((void**)&buckets[i], O_DIRECT_MULTIPLE, buffer_size * sizeof(T));
            buckets[i] = (T *) bucket_allocator::alloc();
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
                size_t bucket_index = assigner(data[i]);
                // move data to bucket
                buckets[bucket_index][buffer_index[bucket_index]++] = data[i];
                // flush if bucket is full
                if (buffer_index[bucket_index] == buffer_size) {
                    buffer_index[bucket_index] = 0;
                    intermediate_writer.Write(bucket_index, buckets[bucket_index], buffer_size);
                    buckets[bucket_index] = (T *) bucket_allocator::alloc();
                }
            }
            free(data);
        }
        // cleanup partially full buckets
        for (size_t i = 0; i < num_buckets; i++) {
            // if bucket is empty, free and do nothing else; otherwise send to writer
            if (buffer_index[i] == 0) {
                bucket_allocator::free((BucketData *) buckets[i]);
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
    FileInfo ProcessBucket(const FileInfo &file_info, const std::string &target_file,
                           const std::function<void(T**, size_t)> processor) {
        // use parlay's sorting utility to sort this bucket
        T *buffer = (T *) ReadEntireFile(file_info.file_name, file_info.file_size);
        size_t n = file_info.true_size / sizeof(T);
        processor(&buffer, n);
        int fd = open(target_file.c_str(), O_WRONLY | O_DIRECT | O_CREAT, 0744);
        SYSCALL(fd);
        SYSCALL(write(fd, buffer, file_info.file_size));
        close(fd);
        free(buffer);
        return {target_file, file_info};
    }

    // reader for the input files
    UnorderedFileReader<T> reader;
    // writer to handle all the buckets created in phase 1 of sample sort
    OrderedFileWriter<T, SAMPLE_SORT_BUCKET_SIZE> intermediate_writer;

public:

    std::vector<FileInfo> Sort(std::vector<FileInfo> &input_files,
                               const std::string &result_prefix,
                               size_t num_buckets,
                               const std::function<size_t(const T&)> assigner,
                               const std::function<void(T**, size_t)> processor) {
        parlay::internal::timer timer("Sample sort internal", true);
        reader.PrepFiles(input_files);
        reader.Start();
        // FIXME: change this file name to a different one (possibly randomized?)
        intermediate_writer.Initialize("spfx_", num_buckets, 1 << 20);
        timer.next("After sampling and before assign to bucket");
        std::vector<FileInfo> bucket_list;
        parlay::par_do([&]() {
            parlay::parallel_for(0, 2, [&](size_t i){
                intermediate_writer.RunIOThread(&intermediate_writer);
            }, 1);
        }, [&]() {
            parlay::parallel_for(0, THREAD_COUNT, [&](int i) {
                AssignToBucket(num_buckets, assigner);
            }, 1);
            // retrieve buckets from intermediate_writer
            bucket_list = intermediate_writer.ReapResult();
        });
        bucket_allocator::finish();
        timer.next("After assign to bucket and before phase 2");
        // FIXME: should allow as much parallelism as internal memory allows; need to calculate memory budget
        //   also, this can get tricky since using a conditional variable to guard against memory overflow
        //   may result in suboptimal performance
        parlay::sequence<FileInfo> results = parlay::map(parlay::iota(bucket_list.size()), [&](size_t i) {
            auto file_info = bucket_list[i];
            auto result_name = GetFileName(result_prefix, i);
            ProcessBucket(file_info, result_name, processor);
            return FileInfo(result_name, file_info);
        });
        timer.next("Sorting complete");
        timer.stop();
        return {results.begin(), results.end()};
    }
};

#endif //SORTING_SCATTER_GATHER_H
