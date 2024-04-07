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

/**
 * Not really an ordered file writer. This class creates many buckets. Each bucket corresponds to a file.
 * Data sent to the same file are not written in order, but the order of the buckets are respected.
 *
 * @tparam T
 */
template<typename T>
class OrderedFileWriter {
    struct Bucket;
    struct IOVectorRequest;
public:
    /**
     * If the default constructor is used, need to call Initialize later
     */
    OrderedFileWriter() = default;

    OrderedFileWriter(std::string &prefix, size_t num_buckets, size_t flush_threshold) {
        Initialize(prefix, num_buckets, flush_threshold);
    }

    ~OrderedFileWriter() {
        CleanUp();
        for (size_t i = 0; i < num_buckets; i++) {
            buckets[i].~Bucket();
        }
        free(buckets);
    }

    /**
     * Initialize the file writer
     *
     * @param prefix Prefix of output file names
     * @param bucket_count Number of buckets to create (they're numbered starting from 0)
     * @param file_flush_threshold The threshold (in bytes) at which a chunk of data is sent to disk
     */
    void Initialize(const std::string &prefix, size_t bucket_count, size_t file_flush_threshold) {
        this->num_buckets = bucket_count;
        // allocate memory for all buckets
        buckets = (Bucket*)malloc(bucket_count * sizeof(Bucket));
        for (size_t i = 0; i < bucket_count; i++) {
            std::string f_name = GetFileName(prefix, i);
            // do a placement new since the std::mutex in a Bucket can't be copied or moved
            new(&buckets[i]) Bucket(f_name, file_flush_threshold);
            // construct the result file; file sizes will be filled in later
            result_files.emplace_back(f_name, 0, 0);
        }
    }

    /**
     * Clean up half-full buckets and misaligned writes in each bucket. Must be called before writer exits to ensure
     * that everything is written to disk. This should not be called concurrently.
     */
    void CleanUp() {
        if (cleanup_started) {
            return;
        }
        cleanup_started = true;
        parlay::parallel_for(0, num_buckets, [&](size_t i) {
            buckets[i].CleanUp();
            result_files[i].file_size = buckets[i].file_size;
            result_files[i].true_size = buckets[i].true_file_size;
        });
    }

    /**
     * Write data to a particular bucket in the writer.
     *
     * Do not submit arrays whose size is not a multiple of O_DIRECT_MULTIPLE unless necessary.
     * @param bucket_number
     * @param pointer Ownership of pointer will now be assumed by the writer, which will deallocate it using free.
     * @param count
     */
    void Write(size_t bucket_number, T *pointer, size_t count) {
        buckets[bucket_number].AddRequest(pointer, count);
    }

    [[nodiscard]] std::vector<FileInfo> ReapResult() {
        CleanUp();
        return std::move(result_files);
    }

private:
    bool cleanup_started = false;
    size_t num_buckets = 0;
    std::vector<FileInfo> result_files;
    Bucket *buckets;

    struct IOVectorRequest {
        size_t current_size = 0;
        // FIXME: add doc on why using iovec
        iovec io_vectors[IO_VECTOR_SIZE];
        uint32_t iovec_count = 0;

        void FreePointers() {
            for (size_t i = 0; i < iovec_count; i++) {
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
        size_t io_threshold = 0;
        std::vector<IOVectorRequest *> pending_requests;
        std::vector<IOVectorRequest *> completed_requests;
        std::vector<std::pair<T *, size_t>> misaligned_pointers;

        Bucket() = delete;

        // FIXME: magic numbers here
        explicit Bucket(std::string &file_name, size_t io_threshold) : max_requests(3), io_threshold(io_threshold) {
            io_uring_queue_init(3, &ring, IORING_SETUP_SQPOLL);
            request = new IOVectorRequest();
            requests_created = 1;
            current_file = open(file_name.c_str(), O_WRONLY | O_DIRECT | O_CREAT, 0744);
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

        /**
         * Reap the completion queue of io_uring
         * @param wait_second Number of seconds to wait
         * @param wait_nanosecond Number of nanoseconds to wait
         */
        void ReapRing(long long wait_second = 0, long long wait_nanosecond = 0) {
            while (true) {
                io_uring_cqe *cqe;
                struct __kernel_timespec wait_time{wait_second, wait_nanosecond};
                int res = io_uring_wait_cqe_timeout(&ring, &cqe, &wait_time);
                if (res != 0) {
                    // no more cqe entries to reap
                    break;
                }
                io_uring_cqe_seen(&ring, cqe);
                SYSCALL(cqe->res);
                auto completed_request = (IOVectorRequest *) io_uring_cqe_get_data(cqe);
                requests_in_ring--;
                completed_requests.push_back(completed_request);
                completed_request->FreePointers();
            }
        }

        /**
         * Wait until no requests are pending and no requests are being processed in the io_uring
         */
        void Wait() {
            while (!pending_requests.empty() || requests_in_ring > 0) {
                while (requests_in_ring > 0) {
                    ReapRing(1);
                }
                if (!pending_requests.empty()) {
                    SubmitToRing();
                }
            }
        }

        /**
         * Try submit a pending request to the ring
         */
        void SubmitToRing() {
            while (!pending_requests.empty() && requests_in_ring < 1) {
                IOVectorRequest *ready_request = pending_requests.back();
                pending_requests.pop_back();
                io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                if (sqe == nullptr) {
                    [[unlikely]]
                    LOG(ERROR) << "io_uring does not have enough sqe";
                    return;
                }
                io_uring_sqe_set_data(sqe, ready_request);
                io_uring_prep_writev(sqe, current_file, &ready_request->io_vectors[0], ready_request->iovec_count, file_size);
                file_size += ready_request->current_size;
                SYSCALL(io_uring_submit(&ring));
                requests_in_ring++;
            }
        }

        /**
         * Create a new iovec request
         * @return Pointer to the new request
         */
        IOVectorRequest *NewRequest() {
            // can't create too many requests; otherwise may exceed memory limit
            // TODO: maybe we can have some request pool shared across all buckets?
            if (requests_created < max_requests) {
                requests_created++;
                return new IOVectorRequest();
            }
            // if we reached the limit, we can only reuse an old request
            while (completed_requests.empty()) {
                ReapRing(0, 1000);
            }
            auto result = completed_requests.back();
            completed_requests.pop_back();
            return result;
        }

        /**
         * Signify that the current request is ready to be written to disk. The request will be sent to
         * pending_requests and the value will be set to nullptr.
         * @param is_last_request True if this is the last request to be submitted.
         */
        void RequestReady(bool is_last_request = false) {
            if (request->current_size > 0) {
                [[likely]]
                pending_requests.push_back(request);
                ReapRing();
                SubmitToRing();
            }
            if (is_last_request) {
                [[unlikely]]
                if (request->current_size == 0) {
                    completed_requests.push_back(request);
                }
                request = nullptr;
            } else {
                request = NewRequest();
            }
        }

        /**
         * Add a new write request in this bucket. This function is thread-safe.
         *
         * Do not submit arrays whose size is not a multiple of O_DIRECT_MULTIPLE unless necessary
         * @param pointer
         * @param count Number of elements in pointer
         */
        inline void AddRequest(T *pointer, size_t count) {
            std::lock_guard<std::mutex> bucket_lock(lock);
            auto size = count * sizeof(T);
            // misaligned pointer need special treatment since they won't fit in a iovec
            if (size % O_DIRECT_MULTIPLE != 0) {
                misaligned_pointers.emplace_back(pointer, count);
                return;
            }
            request->io_vectors[request->iovec_count].iov_base = pointer;
            request->io_vectors[request->iovec_count].iov_len = size;
            request->iovec_count += 1;
            request->current_size += size;
            if (request->current_size >= io_threshold || request->iovec_count >= IO_VECTOR_SIZE) {
                RequestReady();
            }
        }

        /**
         * Write misaligned pointers (those whose size cannot be divided by O_DIRECT_MULTIPLE) to the file.
         *
         * Copies the content in each pointer into a buffer and then writes the buffer to disk using
         * synchronous IO.
         */
        void FlushMisalignedPointers() {
            size_t write_size = 0;
            for (size_t i = 0; i < misaligned_pointers.size(); i++) {
                auto [pointer, count] = misaligned_pointers[i];
                size_t pointer_size = count * sizeof(T);
                write_size += pointer_size;
            }
            size_t target_write_size = (write_size / O_DIRECT_MULTIPLE + 1) * O_DIRECT_MULTIPLE;
            if (target_write_size - write_size < METADATA_SIZE) {
                target_write_size += O_DIRECT_MULTIPLE;
            }
            size_t byte_diff = target_write_size - write_size;

            uint8_t write_buffer[target_write_size];
            size_t buffer_position = 0;
            for (size_t i = 0; i < misaligned_pointers.size(); i++) {
                auto [pointer, count] = misaligned_pointers[i];
                size_t pointer_size = count * sizeof(T);
                memcpy(write_buffer + buffer_position, pointer, pointer_size);
                buffer_position += pointer_size;
            }
            *(uint16_t*)(&write_buffer[target_write_size - METADATA_SIZE]) = (uint16_t)byte_diff;
            // FIXME: performance bottleneck here? parlay does not know to let a thread yield when it's doing
            //   synchronous IO
            lseek64(current_file, file_size, SEEK_SET);
            SYSCALL(write(current_file, write_buffer, target_write_size));
            for (size_t i = 0 ; i < misaligned_pointers.size(); i++) {
                free(misaligned_pointers[i].first);
            }
            // compute true file size (excluding garbage bytes and marker at the end) and file size on disk
            true_file_size = file_size + write_size;
            file_size += target_write_size;
        }

        void CleanUp() {
            // flush the last request and wait for it to complete
            RequestReady(true);
            Wait();
            // flush misaligned pointers
            FlushMisalignedPointers();
        }
    };
};

#endif //SORTING_ORDERED_FILE_WRITER_H
