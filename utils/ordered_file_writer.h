//
// Created by peter on 3/18/24.
//

#ifndef SORTING_ORDERED_FILE_WRITER_H
#define SORTING_ORDERED_FILE_WRITER_H

#include <sys/uio.h>

#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <liburing.h>

#include "config.h"

template <typename T>
class OrderedFileWriter {
    struct Bucket;
    struct IOVectorRequest;
public:
    OrderedFileWriter(std::string &prefix, size_t num_buckets, size_t flush_threshold) {

    }

    void Write(size_t bucket, T* pointer, size_t count) {
        // acquire a bucket specific lock and then work on it
    }

    std::unique_ptr<std::string[]> ReapResult() {
        return std::move(result_files);
    }

private:
    std::unique_ptr<std::string[]> result_files;

    struct IOVectorRequest {
        FILE *current_file;
        size_t current_size;
        iovec io_vectors[IO_VECTOR_SIZE];
        uint32_t iovec_count;
    };

    struct Bucket {
        // FIXME: should a bucket have its own ring or use a common ring?
        //   should we allow requests to queue up?
        //   do we use a single file for a bucket or multiple files?
        std::mutex lock;
        io_uring ring;
        IOVectorRequest *request;
        std::vector<IOVectorRequest*> pending_requests;
        std::vector<IOVectorRequest*> completed_requests;
    };
};

#endif //SORTING_ORDERED_FILE_WRITER_H
