//
// Created by peter on 3/6/24.
//

#include "random_number_generator.h"
#include "parlay/parallel.h"

#include <random>

typedef int NumType;

void WriteUniformRandomNumbers(UnorderedFileWriter<NumType> *writer, size_t count, size_t granularity) {
    std::random_device device;
    std::mt19937 rng(device());
    std::uniform_int_distribution<NumType> distribution(INT32_MIN, INT32_MAX);
    while (count > 0) {
        auto array_size = std::min(granularity, count);
        auto array = std::make_unique<NumType[]>(array_size);
        count -= array_size;
        for (size_t i = 0; i < array_size; i++) {
            array.get()[i] = distribution(rng);
        }
        writer->Push(std::move(array), array_size);
    }
}

void GenerateUniformRandomNumbers(const std::string &prefix, size_t count, size_t nums_per_thread) {
    auto writer = std::make_unique<UnorderedFileWriter<NumType>>(prefix);
    if (nums_per_thread == 0) {
        // TODO: we should let parlay decide; is there a minimum granularity we want here?
        nums_per_thread = std::max(1UL << 20, count / 256);
    }
    nums_per_thread = std::min(nums_per_thread, count);
    parlay::parallel_for(0, count / nums_per_thread, [&](int i){
        WriteUniformRandomNumbers(writer.get(), nums_per_thread, nums_per_thread / 10);
    });
}
