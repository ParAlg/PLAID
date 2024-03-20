//
// Created by peter on 3/18/24.
//

#ifndef SORTING_ORDERED_FILE_WRITER_H
#define SORTING_ORDERED_FILE_WRITER_H

#include <fcntl.h>
#include <sys/uio.h>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <liburing.h>

#include "absl/log/log.h"

#include "utils/logger.h"
#include "config.h"

template<typename T>
class OrderedFileWriter {
    struct Bucket;
    struct IOVectorRequest;
public:
    OrderedFileWriter(std::string &prefix, size_t num_buckets, size_t flush_threshold) :
            num_buckets(num_buckets), flush_threshold(flush_threshold) {
        for (size_t i = 0; i < num_buckets; i++) {
            auto f_name = prefix + "_" + std::to_string(i);
            buckets.emplace_back(f_name, 3);
            result_files.push_back(f_name);
        }
    }

    void Write(size_t bucket_number, T *pointer, size_t count) {
        buckets[bucket_number].AddRequest(flush_threshold, pointer, count);
    }

    std::vector<std::string> ReapResult() {
        return std::move(result_files);
    }

private:
    size_t num_buckets;
    size_t flush_threshold;
    std::vector<std::string> result_files;
    std::vector<Bucket> buckets;

    struct IOVectorRequest {
        size_t current_size = 0;
        iovec io_vectors[IO_VECTOR_SIZE];
        uint32_t iovec_count = 0;

        void FreePointers() {
            for (int i = 0; i < iovec_count; i++) {
                // FIXME: this assumes that the pointer is constructed with a new instead of
                //   (1) a new T[]
                //   (2) a malloc/calloc
                delete (T *) io_vectors[i].iov_base;
            }
        }
    };

    struct Bucket {
        // FIXME: should a bucket have its own ring or use a common ring?
        //   should we allow requests to queue up?
        //   do we use a single file for a bucket or multiple files?
        size_t requests_in_ring = 0;
        size_t requests_created = 0;
        size_t max_requests;
        std::mutex lock;
        io_uring ring;
        IOVectorRequest *request;
        int current_file;
        std::vector<IOVectorRequest *> pending_requests;
        std::vector<IOVectorRequest *> completed_requests;
        std::vector<std::pair<T *, size_t>> misaligned_pointers;

        Bucket(std::string &file_name, size_t max_requests) : max_requests(max_requests) {
            io_uring_queue_init(max_requests, &ring, IORING_SETUP_SQPOLL);
            request = new IOVectorRequest();
            current_file = open(file_name.c_str(), O_WRONLY | O_DIRECT | O_CREAT);
            SYSCALL(current_file);
        }

        ~Bucket() {
            io_uring_queue_exit(&ring);
            for (auto r: completed_requests) {
                delete r;
            }
            if (request != nullptr || !pending_requests.empty() || completed_requests.size() != requests_created) {
                [[unlikely]]
                LOG(ERROR) << "All requests in bucket should be completed by destruction.";
            }
        }

        void ReapRing(long long wait_nanosecond = 0) {
            while (true) {
                io_uring_cqe *cqe;
                struct __kernel_timespec wait_time{0, wait_nanosecond};
                int res = io_uring_wait_cqe_timeout(&ring, &cqe, &wait_time);
                if (res != 0) {
                    // no more cqe entries to reap
                    break;
                }
                io_uring_cqe_seen(&ring, cqe);
                auto completed_request = (IOVectorRequest *) io_uring_cqe_get_data(cqe);
                requests_in_ring--;
                completed_requests.push_back(completed_request);
                completed_request->FreePointers();
            }
        }

        void SubmitToRing() {
            while (!pending_requests.empty() && requests_in_ring < 1) {
                IOVectorRequest *ready_request = pending_requests.pop_back();
                // NOLINTNEXTLINE: disable clang-tidy uninitialized record warning
                io_uring_sqe sqe;
                io_uring_sqe_set_data(&sqe, ready_request);
                io_uring_prep_writev(&sqe, current_file, ready_request->io_vectors, ready_request->iovec_count, 0);
                SYSCALL(io_uring_submit(&ring));
                requests_in_ring++;
            }
        }

        IOVectorRequest *NewRequest() {
            if (requests_created < max_requests) {
                requests_created++;
                return new IOVectorRequest();
            }
            while (completed_requests.empty()) {
                ReapRing(1000);
            }
            return completed_requests.pop_back();
        }

        void FlushRequest() {
            pending_requests.push_back(request);
            ReapRing();
            SubmitToRing();
            request = NewRequest();
        }

        inline void AddRequest(size_t io_threshold, T *pointer, size_t count) {
            std::lock_guard<std::mutex> bucket_lock(lock);
            auto size = count * sizeof(T);
            if (size % O_DIRECT_MULTIPLE != 0) {
                misaligned_pointers.emplace_back(pointer, count);
                return;
            }
            request->io_vectors[request->iovec_count].iov_base = pointer;
            request->io_vectors[request->iovec_count].iov_len = size;
            request->current_size += size;
            if (request->current_size >= io_threshold || request->iovec_count >= IO_VECTOR_SIZE) {
                FlushRequest();
            }
        }
    };
};

#endif //SORTING_ORDERED_FILE_WRITER_H
