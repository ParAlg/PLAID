//
// Created by peter on 3/2/24.
//

#ifndef SORTING_UNORDERED_FILE_READER_H
#define SORTING_UNORDERED_FILE_READER_H

#include "utils/logger.h"
#include "utils/file_utils.h"
#include "configs.h"
#include "utils/simple_queue.h"
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <deque>
#include <liburing.h>
#include <utility>
#include <iostream>
#include <map>
#include <fcntl.h>

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
     *
     * @param buffer_queue_size Size of the buffer queue.
     * Larger values may improve performance at the cost of increased memory usage
     */
    explicit UnorderedFileReader(size_t buffer_queue_size = 1024) {
        buffer_queue.SetSizeLimit(buffer_queue_size);
    }

    ~UnorderedFileReader() {
        is_open = false;
        Wait();
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
        CHECK(num_io_threads > 0) << "Need at least 1 thread";
        active_threads = (int)num_io_threads;
        for (size_t i = 0; i < num_io_threads; i++) {
            std::vector<FileInfo> file_list;
            for (size_t j = i; j < files.size(); j += num_io_threads) {
                file_list.push_back(files[j]);
            }
            worker_threads.push_back(
                    std::make_unique<std::thread>(RunFileReaderWorker, this, std::move(file_list),
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
        buffer_queue.Push({data, size});
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
    std::pair<T*, size_t> Poll() {
        static std::pair<T*, size_t> default_result(nullptr, 0);
        return buffer_queue.Poll(default_result).first;
    }

    void Close() {
        buffer_queue.Close();
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
    std::atomic<int> active_threads = 0;
    // list of files to read
    std::vector<FileInfo> files;
    // files that are available to tbe reused
    std::vector<FileInfo> available_files;
    // a single worker thread for managing file reading
    std::vector<std::unique_ptr<std::thread>> worker_threads;
    // a buffer queue containing data read from disk
    SimpleQueue<std::pair<T*, size_t>> buffer_queue;

    /**
     * A file that is currently being read
     */
    struct OpenedFile {
        int fd;
        size_t bytes_completed = 0, bytes_issued = 0;
        size_t file_size;

        OpenedFile(const std::string& name, size_t file_size) : file_size(file_size) {
            fd = open(name.c_str(), O_DIRECT | O_RDONLY);
            SYSCALL(fd);
        }

        ~OpenedFile() {
            SYSCALL(close(fd));
        }
    };

    struct ReadRequest {
        size_t read_size;
        OpenedFile *file;
        T* data;
    };

    static void RunFileReaderWorker(UnorderedFileReader *reader,
                                    std::vector<FileInfo> &&all_files,
                                    const size_t read_array_size,
                                    const size_t io_uring_size,
                                    const size_t max_outstanding_requests) {
        struct io_uring ring;
        SYSCALL(io_uring_queue_init(io_uring_size, &ring, IORING_SETUP_SINGLE_ISSUER));

        std::deque<OpenedFile*> available_files;
        std::vector<OpenedFile*> completed_files;
        for (auto & file : all_files) {
            available_files.push_back(new OpenedFile(file.file_name, file.file_size));
        }

        size_t outstanding_requests = 0;
        auto *request_pool = (ReadRequest*)malloc(max_outstanding_requests * sizeof(ReadRequest));
        std::vector<ReadRequest*> available_requests;
        available_requests.reserve(max_outstanding_requests);
        for (size_t i = 0; i < max_outstanding_requests; i++) {
            available_requests.push_back(request_pool + i);
        }

        // if we haven't finished all the files and the reader is still open, keep looping
        while (completed_files.size() < all_files.size() && reader->is_open) {
            // reap io_uring result until there's nothing to reap
            while (outstanding_requests > 0) {
                // wait for 0 seconds for the cqe
                struct io_uring_cqe *cqe;
                struct __kernel_timespec wait_time{0, 0};
                int wait_result = io_uring_wait_cqe_timeout(&ring, &cqe, &wait_time);
                if (wait_result == 0) {
                    // process this cqe
                    SYSCALL(cqe->res);
                    auto *request = (ReadRequest *) io_uring_cqe_get_data(cqe);
                    // add data to buffer queue
                    reader->Push(request->data, request->read_size / sizeof(T));
                    auto *file = request->file;
                    file->bytes_completed += request->read_size;
                    if (file->bytes_completed == file->file_size) {
                        completed_files.push_back(file);
                    }
                    available_requests.push_back(request);
                    outstanding_requests--;
                    io_uring_cqe_seen(&ring, cqe);
                } else {
                    // nothing to reap, break
                    break;
                }
            }
            // keep submitting new read requests to the io_uring until we are about to exceed to max size of the buffer
            while (!available_requests.empty() && !available_files.empty()) {
                auto request = available_requests.back();
                available_requests.pop_back();

                auto file = available_files.front();
                available_files.pop_front();
                request->file = file;
                auto read_size = std::min(read_array_size * sizeof(T), file->file_size - file->bytes_issued);
                request->read_size = read_size;
                request->data = (T*)malloc(read_size);

                // issue a read on an opened file
                struct io_uring_sqe *sqe;
                sqe = io_uring_get_sqe(&ring);
                if (sqe == nullptr) {
                    LOG(ERROR) << "Request obtained but is unable to be fulfilled because the io_uring buffer is full.";
                    break;
                }

                // perform write
                io_uring_prep_read(sqe, file->fd, request->data, read_size, file->bytes_issued);
                io_uring_sqe_set_data(sqe, request);
                io_uring_submit(&ring);

                file->bytes_issued += read_size;
                // keep reusing this file if it's not done
                if (file->bytes_issued != file->file_size) {
                    available_files.push_back(file);
                }
                outstanding_requests++;
            }
        }
        // cleanup
        io_uring_queue_exit(&ring);

        free(request_pool);
        CHECK(completed_files.size() == all_files.size())
            << "Expected all files to be read when reader thread terminates";
        for (auto &file : completed_files) {
            delete file;
        }

        reader->active_threads--;
        if (reader->active_threads == 0) {
            reader->Close();
        }
    }
};

#endif //SORTING_UNORDERED_FILE_READER_H
