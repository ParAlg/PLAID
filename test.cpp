#include "parlay/alloc.h"
#include <queue>
#include <mutex>

constexpr size_t TOTAL_SIZE = 1UL << 35;
constexpr size_t BLOCK_SIZE = 1 << 14;

struct Test {
    unsigned char data[BLOCK_SIZE];
};
using Allocator = parlay::type_allocator<Test>;

std::queue<Test*> request_queue;
std::mutex mutex;

void ConsumerThread() {
    size_t freed = 0;
    while (freed < TOTAL_SIZE) {
        mutex.lock();
        if (request_queue.empty()) {
            mutex.unlock();
            continue;
        }
        Test* data = request_queue.front();
        request_queue.pop();
        mutex.unlock();
        Allocator::free(data);
        freed += BLOCK_SIZE;
    }
}

void ProducerThread() {
    auto *ptr = Allocator::alloc();
    memset(ptr, 3, BLOCK_SIZE);
    std::lock_guard<std::mutex> lock(mutex);
    request_queue.push((Test*)ptr);
}

int main() {
    std::thread t(ConsumerThread);
    parlay::parallel_for(0, TOTAL_SIZE / BLOCK_SIZE, [&](size_t i){
        ProducerThread();
    }, 1);
    t.join();
}