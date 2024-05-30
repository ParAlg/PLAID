#include "parlay/alloc.h"
#include "parlay/random.h"
#include "utils/simple_queue.h"

constexpr size_t TOTAL_SIZE = 1UL << 38;
constexpr size_t BLOCK_SIZE = 1 << 14;
constexpr size_t QUEUE_COUNT = 128;

struct Writer {
    struct Test {
        unsigned char data[BLOCK_SIZE];
    };
    using Allocator = parlay::type_allocator<Test>;

    SimpleQueue<Test*> request_queue;
    std::thread t;

    static void thread(Writer *writer) {
        size_t freed = 0;
        while (freed < TOTAL_SIZE) {
            while (true) {
                auto data = writer->request_queue.Poll();
                if (data != nullptr) {
                    Allocator::free(data);
                    freed += BLOCK_SIZE;
                } else {
                    break;
                }
            }
        }
    }

    Writer() {
        t = std::thread(thread, this);
    }

    ~Writer() {
        t.join();
    }

    void Add(void *ptr) {
        request_queue.Push((Test*)ptr);
    }
};
Writer *writer;

struct Test {
    unsigned char data[BLOCK_SIZE];
};
using Allocator = parlay::type_allocator<Test>;

void thread() {
    auto *ptr = Allocator::alloc();
    memset(ptr, 3, BLOCK_SIZE);
    writer->Add(ptr);
}

int main() {
    writer = new Writer();
    parlay::parallel_for(0, TOTAL_SIZE / BLOCK_SIZE, [&](size_t i){
        thread();
    }, 1);
    for (size_t i = 0; i < QUEUE_COUNT; i++) {
        writer->request_queue.Close();
    }
    delete writer;
}