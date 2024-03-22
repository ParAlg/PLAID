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
#include "parlay/parallel.h"

#include "utils/logger.h"
#include "config.h"
#include "utils/file_info.h"
#include "utils/file_utils.h"

template<typename T>
class OrderedFileWriter {
    struct Bucket;
    struct IOVectorRequest;
public:
    OrderedFileWriter(std::string &prefix, size_t num_buckets, size_t flush_threshold) :
            num_buckets(num_buckets), flush_threshold(flush_threshold) {
        for (size_t i = 0; i < num_buckets; i++) {
            auto f_name = GetFileName(prefix, i);
            buckets.emplace_back(f_name, 3);
            result_files.emplace_back(f_name, 0, 0);
        }
    }

    ~OrderedFileWriter() {
        FlushRemainingPointers();
    }

    void FlushRemainingPointers() {
        if (cleanup_complete) {
            return;
        }
        cleanup_complete = true;
        parlay::parallel_for(0, buckets.size(), [&](size_t i) {
            buckets[i].FlushRemainingPointers();
            result_files[i].file_size = buckets[i].file_size;
            result_files[i].true_size = buckets[i].true_file_size;
        });
    }

    void Write(size_t bucket_number, T *pointer, size_t count) {
        buckets[bucket_number].AddRequest(flush_threshold, pointer, count);
    }

    std::vector<FileInfo> ReapResult() {
        FlushRemainingPointers();
        return std::move(result_files);
    }

private:
    bool cleanup_complete = false;
    size_t num_buckets;
    size_t flush_threshold;
    std::vector<FileInfo> result_files;
    std::vector<Bucket> buckets;

    struct IOVectorRequest {
        size_t current_size = 0;
        iovec io_vectors[IO_VECTOR_SIZE];
        uint32_t iovec_count = 0;

        void FreePointers() {
            for (int i = 0; i < iovec_count; i++) {
                // FIXME: this assumes that the pointer is constructed with a malloc instead of
                //   (1) a new T[]
                //   (2) a new T
                free(io_vectors[i].iov_base);
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
        size_t file_size = 0;
        size_t true_file_size = 0;
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
            SYSCALL(close(current_file));
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
            file_size += request->current_size;
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

        void FlushRemainingPointers() {
            size_t num_pointers = misaligned_pointers.size() + 1;
            iovec io_vectors[num_pointers];
            size_t write_size = 0;
            for (size_t i = 0; i < misaligned_pointers.size(); i++) {
                auto [pointer, count] = misaligned_pointers[i];
                size_t pointer_size = count * sizeof(T);
                write_size += pointer_size;
                io_vectors[i].iov_base = pointer;
                io_vectors[i].iov_len = pointer_size;
            }
            size_t target_write_size = (write_size / O_DIRECT_MULTIPLE + 1) * O_DIRECT_MULTIPLE;
            if (target_write_size - write_size < METADATA_SIZE) {
                target_write_size += O_DIRECT_MULTIPLE;
            }
            size_t byte_diff = target_write_size - write_size;

            // set up padding at end of file to align with target write size
            int8_t end_file_padding[byte_diff];
            *(uint16_t*)(&end_file_padding[byte_diff - METADATA_SIZE]) = (uint16_t)byte_diff;
            io_vectors[num_pointers - 1].iov_base = end_file_padding;
            io_vectors[num_pointers - 1].iov_len = byte_diff;

            writev(current_file, io_vectors, (int)num_pointers);

            for (size_t i = 0 ; i < num_pointers - 1; i++) {
                free(io_vectors[i].iov_base);
            }

            true_file_size = file_size + write_size;
            file_size += target_write_size;
        }
    };
};

#endif //SORTING_ORDERED_FILE_WRITER_H
