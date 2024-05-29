#include "parlay/alloc.h"
#include "parlay/random.h"
#include "utils/simple_queue.h"

constexpr size_t TOTAL_SIZE = 1UL << 38;
constexpr size_t WRITE_SIZE = 1 << 14;
constexpr size_t QUEUE_COUNT = 128;

struct Writer {
    struct Test {
        unsigned char data[WRITE_SIZE];
    };
    using Allocator = parlay::type_allocator<Test>;

    SimpleQueue<Test*> queue[QUEUE_COUNT];
    std::thread t;

    static void thread(Writer *writer) {
        size_t freed = 0;
        while (freed < TOTAL_SIZE) {
            for (size_t i = 0; i < QUEUE_COUNT; i++) {
                while (true) {
                    auto data = writer->queue[i].Poll();
                    if (data != nullptr) {
                        Allocator::free(data);
                        freed += WRITE_SIZE;
                    } else {
                        break;
                    }
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
};
Writer *writer;

struct Test {
    unsigned char data[WRITE_SIZE];
};
using Allocator = parlay::type_allocator<Test>;

void thread() {
    parlay::random_generator gen;
    std::uniform_int_distribution<size_t> dis(0, QUEUE_COUNT - 1);
    auto *ptr = Allocator::alloc();
    memset(ptr, 3, WRITE_SIZE);
    writer->queue[dis(gen)].Push((Writer::Test*)ptr);
}

int main() {
    writer = new Writer();
    parlay::parallel_for(0, TOTAL_SIZE / WRITE_SIZE, [&](size_t i){
        thread();
    });
    for (size_t i = 0; i < QUEUE_COUNT; i++) {
        writer->queue[i].Close();
    }
    delete writer;
}