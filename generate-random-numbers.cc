#include <iostream>
#include <thread>
#include <random>
#include "utils/unordered_file_writer.h"
#include "parlay/parallel.h"

// 1 billion numbers = 1GB
const size_t TOTAL_NUMBER = 1UL << 15;

const size_t GENERATOR_WORK = 1UL << 10;

// generator 2^20 numbers each time and flush; this would give exactly 4MB writes
const size_t GENERATOR_STEP = 1 << 10;

void MakeRandomNumber(UnorderedFileWriter<int> *writer, size_t count) {
    std::random_device device;
    std::mt19937 rng(device());
    std::uniform_int_distribution<> distribution(INT32_MIN, INT32_MAX);
    while (count > 0) {
        auto array_size = std::min(GENERATOR_STEP, count);
        auto array = std::make_unique<int[]>(array_size);
        count -= array_size;
        for (size_t i = 0; i < array_size; i++) {
            array.get()[i] = distribution(rng);
        }
        std::shared_ptr<int[]> a2 = std::move(array);
        writer->Push(std::move(a2), array_size);
    }
}

int main() {
    InitLogger();
    auto writer = std::make_unique<UnorderedFileWriter<int>>("numbers");
    parlay::parallel_for(0, TOTAL_NUMBER / GENERATOR_WORK, [&](int i){
        MakeRandomNumber(writer.get(), GENERATOR_WORK);
    });
    std::cout << "[1]    114514 Segmentation fault (core dumped)" << std::endl;
    return 0;
}
