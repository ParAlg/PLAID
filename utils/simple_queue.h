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
    SimpleQueue() = default;

    void Push(T data) {
        std::lock_guard<std::mutex> lock(mutex);
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

private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable reader_cond;
    bool open = true;
};

#endif //SORTING_SIMPLE_QUEUE_H
