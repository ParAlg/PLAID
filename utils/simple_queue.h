//
// Created by peter on 5/13/24.
//

#ifndef SORTING_SIMPLE_QUEUE_H
#define SORTING_SIMPLE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class SimpleQueue {
public:
    explicit SimpleQueue(size_t size_limit = 0) : size_limit(size_limit) {}

    void Push(T data) {
        std::unique_lock<std::mutex> lock(mutex);
        while (size_limit && queue.size() == size_limit) {
            writer_cond.wait(lock);
        }
        queue.push(data);
        reader_cond.notify_one();
    }

    T Poll(T default_result = nullptr) {
        std::unique_lock<std::mutex> lock(mutex);
        while (queue.empty()) {
            if (!open) {
                return default_result;
            }
            reader_cond.wait(lock);
        }
        T ret = queue.front();
        queue.pop();
        if (size_limit && queue.size() == size_limit - 1) {
            writer_cond.notify_one();
        }
        return ret;
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex);
        open = false;
        reader_cond.notify_all();
    }

    bool IsEmptyUnsafe() {
        return queue.empty();
    }

    void SetSizeLimit(size_t new_limit) {
        size_limit = new_limit;
    }

private:
    std::queue<T> queue;
    std::mutex mutex;
    size_t size_limit;
    std::condition_variable reader_cond, writer_cond;
    bool open = true;
};

#endif //SORTING_SIMPLE_QUEUE_H
