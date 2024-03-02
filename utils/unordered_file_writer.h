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

template <typename T>
struct WriteRequest {
    std::shared_ptr<T*> data;
    size_t size;
};

template <typename T>
class UnorderedFileWriter {
public:
    explicit UnorderedFileWriter(std::string &&prefix, int buffer_size = IO_URING_BUFFER_SIZE) : prefix(std::move(prefix)) {
        worker_thread = std::make_unique<std::thread>(RunFileWriterWorker, this, buffer_size);
    }

    ~UnorderedFileWriter() {
        open = false;
        worker_thread->join();
    }

    void Write(std::shared_ptr<T*> &&data, size_t size) {
        std::lock_guard<std::mutex> guard(wait_queue_lock);
        wait_queue.emplace_back(data, size);
        wait_queue_cond.notify_one();
    }

    WriteRequest<T> Poll() {
        std::unique_lock<std::mutex> lock(wait_queue_lock);
        lock.lock();
        if (wait_queue.empty()) {
            wait_queue_cond.wait_for(lock, std::chrono::milliseconds(1));
            if (wait_queue.empty()) {
                return {nullptr, 0};
            }
        }
        auto result = wait_queue.front();
        wait_queue.pop_front();
        return result;
    }

    void Close() {
        open = false;
    }

    bool IsOpen() {
        return open;
    }

private:
    bool open = true;
    std::unique_ptr<std::thread> worker_thread;
    std::string prefix;
    // TODO: if this ever becomes the bottleneck, we can use a lock-free queue instead
    std::deque<WriteRequest<T>> wait_queue;
    std::mutex wait_queue_lock;
    std::condition_variable wait_queue_cond;

    static void RunFileWriterWorker(UnorderedFileWriter *writer, size_t buffer_size) {
        struct io_uring ring{};
        SYSCALL(io_uring_queue_init(buffer_size, &ring, IORING_SETUP_SQPOLL));

        const auto prefix = writer->prefix;

        while (writer->IsOpen()) {
            auto request = writer->Poll();
            // reap io_uring result

            if (request.size == 0) {
                continue;
            }

            int fd = open((prefix + "1").c_str(), O_DIRECT | O_WRONLY);

            close(fd);
            // add writer request to ring
        }

        io_uring_queue_exit(&ring);
    }
};

#endif //SORTING_UNORDERED_FILE_WRITER_H
