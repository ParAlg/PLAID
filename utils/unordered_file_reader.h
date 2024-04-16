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

/**
 * Read a list of files in no particular order. File sizes are assumed to be multiples of O_DIRECT_MULTIPLE and
 * all bytes in the file are plain data (no end-of-file size markers are used and there are not padding bytes).
 *
 * @tparam T The data type to be read from the file.
 */
template<typename T>
class UnorderedFileReader {
public:
    /**
     * Creates the object. Note that you should call PrepFiles and Start to really begin the file reader.
     */
    explicit UnorderedFileReader() {
        buffer_queue_size = 50;
    }

    ~UnorderedFileReader() {
        is_open = false;
        Wait();
    }

    void SetBufferQueueSize(size_t new_size) {
        buffer_queue_size = new_size;
    }

    void PrepFiles(const std::string& prefix) {
        files = FindFiles(prefix);
    }

    void PrepFiles(const std::vector<FileInfo>& file_list) {
        this->files = file_list;
    }

    void Start(size_t array_size = 1 << 20,
               size_t max_outstanding_requests = IO_URING_BUFFER_SIZE,
               size_t io_uring_size = IO_URING_BUFFER_SIZE,
               size_t num_io_threads = 1) {
        for (size_t i = 0; i < num_io_threads; i++) {
            worker_threads.push_back(
                    std::make_unique<std::thread>(RunFileReaderWorker, this, files,
                                                  array_size, io_uring_size, max_outstanding_requests));
        }
    }

    /**
     * Add new piece of data to the buffer
     *
     * @param data
     * @param size
     */
    void Push(T* data, size_t size) {
        std::unique_lock<std::mutex> lock(buffer_lock);
        while (buffer_queue.size() >= buffer_queue_size) {
            buffer_writer_cond.wait(lock);
        }
        buffer_queue.emplace_back(data, size);
        buffer_reader_cond.notify_one();
    }

    /**
     * Get a piece of data from the buffer. This call will hang until
     *   (1) a piece of data is available
     *   (2) the reader is closed (because some other thread closed it or because it ran out of data to read)
     * This function is thread-safe.
     *
     * @param timeout_microseconds The interval for checking whether the reader is still open.
     * @return pointer to a piece of data and its size if one is available; nullptr and 0 if the reader is closed
     *   and no longer has any data
     */
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
     * Block until file reader finishes
     */
    void Wait() {
        for (auto &t : worker_threads) {
            if (t->joinable()) {
                t->join();
            }
        }
    }

private:
    // whether the file reader is actively running
    bool is_open = true;
    // list of files to read
    std::vector<FileInfo> files;
    // size of the buffer queue; larger values may improve performance at the cost of increased memory usage
    size_t buffer_queue_size;
    // files that are available to tbe reused
    std::vector<FileInfo> available_files;
    // a single worker thread for managing file reading
    std::vector<std::unique_ptr<std::thread>> worker_threads;
    // TODO: if this ever becomes the bottleneck, we can use a lock-free queue instead
    // a buffer queue containing data read from disk
    std::deque<std::pair<T*, size_t>> buffer_queue;
    std::mutex buffer_lock;
    std::condition_variable buffer_reader_cond, buffer_writer_cond;

    /**
     * A file that is currently being read
     */
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
            SYSCALL(close(fd));
        }
    };

    static void RunFileReaderWorker(UnorderedFileReader *reader,
                                    std::vector<FileInfo> available_files,
                                    const size_t read_array_size,
                                    const size_t io_uring_size,
                                    const size_t max_outstanding_requests) {
        struct io_uring ring;
        SYSCALL(io_uring_queue_init(io_uring_size, &ring, IORING_SETUP_SQPOLL));
        size_t total_files = available_files.size();
        size_t completed_files = 0, busy_files = 0;
        std::deque<OpenedFile *> free_files;

        // if we haven't finished all the files and the reader is still open, keep looping
        while (completed_files < total_files && reader->is_open) {
            // reap io_uring result until there's nothing to reap
            while (busy_files > 0) {
                // wait for 0 seconds for the cqe
                struct io_uring_cqe *cqe;
                struct __kernel_timespec wait_time{0, 0};
                int wait_result = io_uring_wait_cqe_timeout(&ring, &cqe, &wait_time);
                if (wait_result == 0) {
                    // process this cqe
                    SYSCALL(cqe->res);
                    auto *file = (OpenedFile *) io_uring_cqe_get_data(cqe);
                    busy_files--;
                    // add data to buffer queue
                    reader->Push(file->data, file->current_read_size / sizeof(T));
                    file->data = nullptr;
                    file->bytes_read += file->current_read_size;
                    // cleanup file if we have exhausted all the data in it
                    if (file->bytes_read >= file->file_size) {
                        delete file;
                        completed_files++;
                    } else {
                        // file is free to be read again
                        free_files.push_back(file);
                    }
                    io_uring_cqe_seen(&ring, cqe);
                } else {
                    // nothing to reap, break
                    break;
                }
            }
            // keep submitting new read requests to the io_uring until we are about to exceed to max size of the buffer
            while (busy_files < max_outstanding_requests) {
                if (free_files.empty()) {
                    // if no opened file is available, we open a new file
                    if (available_files.empty()) {
                        break;
                    }
                    auto file_info = available_files.back();
                    available_files.pop_back();
                    free_files.push_back(new OpenedFile(file_info.file_name, file_info.file_size));
                }

                // issue a read on an opened file
                struct io_uring_sqe *sqe;
                sqe = io_uring_get_sqe(&ring);
                // this should never happen if we never allow the number of concurrent reads to exceed the size of
                // the io_uring buffer
                if (sqe == nullptr) {
                    LOG(ERROR) << "Request obtained but is unable to be fulfilled because the io_uring buffer is full.";
                    break;
                }

                auto *file = free_files.front();
                free_files.pop_front();

                // either read the specified amount or the remaining size of the file
                auto read_size = std::min(read_array_size * sizeof(T), file->file_size - file->bytes_read);
                file->current_read_size = read_size;
                // allocate memory
                file->data = (T*)malloc(read_size);

                // perform write
                io_uring_prep_read(sqe, file->fd, file->data, read_size, file->bytes_read);
                io_uring_sqe_set_data(sqe, file);
                io_uring_submit(&ring);
                busy_files++;
            }
        }
        // cleanup
        io_uring_queue_exit(&ring);
        reader->Close();
    }
};

#endif //SORTING_UNORDERED_FILE_READER_H
