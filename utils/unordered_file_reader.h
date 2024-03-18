//
// Created by peter on 3/2/24.
//

#ifndef SORTING_UNORDERED_FILE_READER_H
#define SORTING_UNORDERED_FILE_READER_H

#include "utils/logger.h"
#include "utils/file_utils.h"
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
class UnorderedFileReader {
public:
    explicit UnorderedFileReader(const std::string& prefix,
                                 size_t array_size,
                                 int file_count = -1,
                                 size_t buffer_queue_size = 50,
                                 int io_uring_buffer_size = IO_URING_BUFFER_SIZE) : buffer_queue_size(buffer_queue_size) {
        auto files = FindFiles(prefix, file_count);
        worker_thread = std::make_unique<std::thread>(RunFileReaderWorker, this, files, array_size, io_uring_buffer_size);
    }

    ~UnorderedFileReader() {
        is_open = false;
        worker_thread->join();
    }

    void Push(T* data, size_t size) {
        std::unique_lock<std::mutex> lock(buffer_lock);
        while (buffer_queue.size() >= buffer_queue_size) {
            buffer_writer_cond.wait(lock);
        }
        buffer_queue.emplace_back(data, size);
        buffer_reader_cond.notify_one();
    }

    std::pair<T*, size_t> Poll(size_t timeout_microseconds = 1000) {
        std::unique_lock<std::mutex> lock(buffer_lock);
        while (buffer_queue.empty()) {
            buffer_reader_cond.wait_for(lock, std::chrono::microseconds(timeout_microseconds));
            if (!IsOpen()) {
                return {nullptr, 0};
            }
        }
        auto result = buffer_queue.front();
        buffer_queue.pop_front();
        buffer_writer_cond.notify_one();
        return result;
    }

    bool IsEmpty() {
        std::lock_guard<std::mutex> lock(buffer_lock);
        return buffer_queue.empty();
    }

    void Close() {
        is_open = false;
    }

    bool IsOpen() {
        return is_open;
    }

    /**
     * Block until file reader finishes and return the number of files
     * @return
     */
    int Wait() {
        worker_thread->join();
        return num_files;
    }

private:
    bool is_open = true;
    size_t num_files = 0, buffer_queue_size;
    std::vector<FileInfo> available_files;
    std::unique_ptr<std::thread> worker_thread;
    // TODO: if this ever becomes the bottleneck, we can use a lock-free queue instead
    std::deque<std::pair<T*, size_t>> buffer_queue;
    std::mutex buffer_lock;
    std::condition_variable buffer_reader_cond, buffer_writer_cond;

    struct OpenedFile {
        int fd;
        size_t bytes_read = 0, current_read_size = 0;
        size_t file_size;
        T* data;

        OpenedFile(const std::string& name, size_t file_size) : file_size(file_size) {
            fd = open(name.c_str(), O_DIRECT | O_RDONLY);
            SYSCALL(fd);
        }

        ~OpenedFile() {
            close(fd);
        }
    };

    static void RunFileReaderWorker(UnorderedFileReader *reader,
                                    std::vector<FileInfo> available_files,
                                    const size_t read_array_size,
                                    const size_t buffer_size) {
        struct io_uring ring;
        SYSCALL(io_uring_queue_init(buffer_size, &ring, IORING_SETUP_SQPOLL));
        size_t total_files = available_files.size();
        size_t completed_files = 0, busy_files = 0;
        std::deque<OpenedFile *> free_files;

        while (completed_files < total_files && reader->is_open) {
            // reap io_uring result
            while (busy_files > 0) {
                struct io_uring_cqe *completion_queue_entry;
                struct __kernel_timespec wait_time{0, 0};
                int wait_result = io_uring_wait_cqe_timeout(&ring, &completion_queue_entry, &wait_time);
                if (wait_result == 0) {
                    SYSCALL(completion_queue_entry->res);
                    auto *file = (OpenedFile *) completion_queue_entry->user_data;
                    busy_files--;
                    reader->Push(file->data, file->current_read_size / sizeof(T));
                    file->data = nullptr;
                    file->bytes_read += file->current_read_size;
                    if (file->bytes_read >= file->file_size) {
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
                // submit this IO request to ring
                if (free_files.empty()) {
                    if (available_files.empty()) {
                        break;
                    }
                    do {
                        auto [file_name, file_size] = available_files.back();
                        available_files.pop_back();
                        free_files.push_back(new OpenedFile(file_name, file_size));
                        // if # files is less than # SSDs, then read them all to avoid waiting on a few files in the end
                    } while(available_files.size() <= SSD_COUNT && !available_files.empty());
                }

                struct io_uring_sqe *submission_queue_entry;
                submission_queue_entry = io_uring_get_sqe(&ring);
                if (submission_queue_entry == nullptr) {
                    LOG(ERROR) << "Request obtained but is unable to be fulfilled because the io_uring buffer is full.";
                    break;
                }

                auto file = free_files.front();
                free_files.pop_front();

                auto read_size = std::min(read_array_size * sizeof(T), file->file_size - file->bytes_read);
                file->current_read_size = read_size;
                file->data = new T[read_size / sizeof(T)];

                io_uring_prep_read(submission_queue_entry, file->fd, file->data, read_size, file->bytes_read);
                io_uring_sqe_set_data(submission_queue_entry, file);
                io_uring_submit(&ring);
                busy_files++;
            }
        }
        io_uring_queue_exit(&ring);
        reader->Close();
        DLOG(INFO) << "FileReader worker thread exited";
    }
};

#endif //SORTING_UNORDERED_FILE_READER_H
