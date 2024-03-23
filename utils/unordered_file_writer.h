//
// Created by peter on 3/2/24.
//

#ifndef SORTING_UNORDERED_FILE_WRITER_H
#define SORTING_UNORDERED_FILE_WRITER_H

#include "utils/logger.h"
#include "config.h"
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <deque>
#include <liburing.h>
#include <utility>
#include <iostream>
#include <map>

template<typename T>
struct WriteRequest {
    std::shared_ptr<T[]> data;
    size_t size;

    WriteRequest(std::shared_ptr<T[]> data, size_t size) : data(std::move(data)), size(size) {}
};

template<typename T>
class UnorderedFileWriter {
public:
    explicit UnorderedFileWriter(std::string prefix,
                                 int buffer_size = IO_URING_BUFFER_SIZE) : prefix(std::move(prefix)) {
        worker_thread = std::make_unique<std::thread>(RunFileWriterWorker, this, buffer_size);
    }

    ~UnorderedFileWriter() {
        is_open = false;
        worker_thread->join();
    }

    void Push(std::shared_ptr<T[]> data, size_t size) {
        // FIXME: need to align writes to 512 byte blocks, otherwise we won't be able to use O_DIRECT
        //   short term solution is to force multiples of 512 and throw an error otherwise;
        //   alternatively, use ftruncate to change the size of the file
        //   long term solution is to store the size of the last section in the end of the file (i.e. last 8 bytes)
        if (size * sizeof(T) % O_DIRECT_MULTIPLE != 0) {
            [[unlikely]]
            LOG(ERROR)
                << "Size (in bytes) must be aligned to the size of a page in O_DIRECT mode. "
                << "Actual size: " << size * sizeof(T);
        }
        std::lock_guard<std::mutex> guard(wait_queue_lock);
        wait_queue.emplace_back(std::move(data), size);
        wait_queue_cond.notify_one();
    }

    WriteRequest<T> Poll(size_t timeout_microseconds = 1000) {
        std::unique_lock<std::mutex> lock(wait_queue_lock);
        if (wait_queue.empty()) {
            wait_queue_cond.wait_for(lock, std::chrono::microseconds(timeout_microseconds));
            if (wait_queue.empty()) {
                return {nullptr, 0};
            }
        }
        auto result = wait_queue.front();
        wait_queue.pop_front();
        return result;
    }

    bool IsEmpty() {
        std::lock_guard<std::mutex> lock(wait_queue_lock);
        return wait_queue.empty();
    }

    void Close() {
        is_open = false;
    }

    bool IsOpen() {
        return is_open;
    }

    /**
     * Block until file intermediate_writer finishes and return the number of files
     * @return
     */
    int Wait() {
        worker_thread->join();
        return num_files;
    }

private:
    bool is_open = true;
    int num_files = 0;
    std::unique_ptr<std::thread> worker_thread;
    std::string prefix;
    // TODO: if this ever becomes the bottleneck, we can use a lock-free queue instead
    std::deque<WriteRequest<T>> wait_queue;
    std::mutex wait_queue_lock;
    std::condition_variable wait_queue_cond;

    struct OpenedFile {
        int fd;
        char *file_name;
        size_t bytes_written = 0;
        std::shared_ptr<T[]> data;

        explicit OpenedFile(const std::string &&name) {
            file_name = (char*)malloc(name.size() + 1);
            strcpy(file_name, name.c_str());
            fd = open(file_name, O_DIRECT | O_WRONLY | O_CREAT, 0744);
            SYSCALL(fd);
        }

        void SetData(std::shared_ptr<T[]> data_ptr) {
            data = std::move(data_ptr);
        }

        ~OpenedFile() {
            close(fd);
            free(file_name);
        }
    };

    static void RunFileWriterWorker(UnorderedFileWriter *writer, const size_t buffer_size) {
        struct io_uring ring;
        SYSCALL(io_uring_queue_init(buffer_size, &ring, IORING_SETUP_SQPOLL));
        size_t file_counter = 0, completed_files = 0, busy_files = 0;
        std::deque<OpenedFile *> free_files;

        char file_name_buffer[255];
        const auto file_name_prefix = writer->prefix.c_str();

        // FIXME: do we need to acquire a lock for the second check?
        while (writer->IsOpen() || !writer->IsEmpty()) {
            // reap io_uring result
            while (busy_files > 0) {
                struct io_uring_cqe *completion_queue_entry;
                struct __kernel_timespec wait_time{0, 0};
                int wait_result = io_uring_wait_cqe_timeout(&ring, &completion_queue_entry, &wait_time);
                if (wait_result == 0) {
                    SYSCALL(completion_queue_entry->res);
                    auto *file = (OpenedFile *) io_uring_cqe_get_data(completion_queue_entry);
                    busy_files--;
                    if (file->bytes_written >= FILE_SIZE) {
                        delete file;
                        completed_files++;
                    } else {
                        free_files.push_back(file);
                    }
                    io_uring_cqe_seen(&ring, completion_queue_entry);
                } else {
                    break;
                }
            }
            while (busy_files < buffer_size) {
                auto request = writer->Poll();
                if (request.size == 0) {
                    break;
                }
                // submit this IO request to ring
                struct io_uring_sqe *submission_queue_entry;
                submission_queue_entry = io_uring_get_sqe(&ring);
                if (submission_queue_entry == nullptr) {
                    LOG(ERROR) << "Request obtained but is unable to be fulfilled because the io_uring buffer is full.";
                    break;
                }
                if (free_files.empty()) {
                    sprintf(file_name_buffer, "/mnt/ssd%lu/%s%08lu",
                            file_counter % SSD_COUNT, file_name_prefix, file_counter);
                    auto new_file = new OpenedFile(file_name_buffer);
                    file_counter++;
                    free_files.push_back(new_file);
                }
                auto file = free_files.front();
                free_files.pop_front();
                file->SetData(std::move(request.data));
                size_t num_bytes = request.size * sizeof(T);
                file->bytes_written += num_bytes;
                io_uring_prep_write(submission_queue_entry, file->fd, file->data.get(), num_bytes, 0);
                io_uring_sqe_set_data(submission_queue_entry, file);
                SYSCALL(io_uring_submit(&ring));
                busy_files++;
            }
        }

        // Wait for remaining IOs to finish before destroying the ring
        // Also avoids memory leaks
        while (busy_files > 0) {
            struct io_uring_cqe *completion_queue_entry;
            int wait_result = io_uring_wait_cqe(&ring, &completion_queue_entry);
            if (wait_result != 0) {
                LOG(ERROR) << "Cannot fetch remaining io_uring entries";
                return;
            }
            SYSCALL(completion_queue_entry->res);
            auto *file = (OpenedFile *) io_uring_cqe_get_data(completion_queue_entry);;
            busy_files--;
            completed_files++;
            delete file;
            io_uring_cqe_seen(&ring, completion_queue_entry);
        }
        while (!free_files.empty()) {
            auto file = free_files.front();
            free_files.pop_front();
            delete file;
        }
        io_uring_queue_exit(&ring);
        DLOG(INFO) << "FileWriter worker thread exited";

        writer->num_files = completed_files;
    }
};

#endif //SORTING_UNORDERED_FILE_WRITER_H
