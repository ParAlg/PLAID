#include "parlay/alloc.h"
#include <queue>
#include <mutex>

constexpr size_t TOTAL_SIZE = 1UL << 38;
constexpr size_t BLOCK_SIZE = 1 << 14;

struct Test {
    unsigned char data[BLOCK_SIZE];
};
using Allocator = parlay::type_allocator<Test>;

struct Consumer {

    std::queue<Test*> request_queue;
    std::mutex mutex;
    std::thread t;

    static void ConsumerThread(Consumer *consumer) {
        size_t freed = 0;
        auto *queue = &consumer->request_queue;
        while (freed < TOTAL_SIZE) {
            while (true) {
                std::lock_guard<std::mutex> lock(consumer->mutex);
                if (queue->empty()) {
                    continue;
                }
                Test* data = queue->front();
                queue->pop();
                if (data != nullptr) {
                    // Allocator::free(data);
                    freed += BLOCK_SIZE;
                } else {
                    break;
                }
            }
        }
    }

    Consumer() {
        t = std::thread(ConsumerThread, this);
    }

    ~Consumer() {
        t.join();
    }

    void Add(void *ptr) {
        std::lock_guard<std::mutex> lock(mutex);
        request_queue.push((Test*)ptr);
    }
};

Consumer *writer;

void thread() {
    auto *ptr = Allocator::alloc();
    memset(ptr, 3, BLOCK_SIZE);
    writer->Add(ptr);
}

int main() {
    writer = new Consumer();
    parlay::parallel_for(0, TOTAL_SIZE / BLOCK_SIZE, [&](size_t i){
        thread();
    }, 1);
    delete writer;
}