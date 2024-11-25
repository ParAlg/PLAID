//
// Created by peter on 3/6/24.
//

#include "random_number_generator.h"
#include "parlay/parallel.h"

#include <random>

template <typename T>
void GenerateUniformRandomNumbers(const std::string &prefix, size_t count) {
    const size_t GRANULARITY = 1 << 20;
    UnorderedFileWriter<T> writer(prefix);
    CHECK(count % GRANULARITY == 0) << "Can only generate numbers that are a multiple of " << GRANULARITY;
    std::atomic<int64_t> num_blocks = (int64_t)(count / GRANULARITY);
    parlay::parallel_for(0, THREAD_COUNT, [&](size_t _){
        std::random_device device;
        std::mt19937 rng(device());
        auto limit = std::numeric_limits<T>();
        std::uniform_int_distribution<T> distribution(limit.min(), limit.max());
        while(true) {
            auto current = num_blocks--;
            if (current <= 0) {
                return;
            }
            std::shared_ptr<T> result((T*)aligned_alloc(O_DIRECT_MULTIPLE, GRANULARITY * sizeof(T)), free);
            for (size_t i = 0; i < GRANULARITY; i++) {
                result.get()[i] = distribution(rng);
            }
            writer.Push(result, GRANULARITY);
        }
    }, 1);
    writer.Close();
}

template void GenerateUniformRandomNumbers<int64_t>(const std::string &prefix, size_t count);
template void GenerateUniformRandomNumbers<uint64_t>(const std::string &prefix, size_t count);
template void GenerateUniformRandomNumbers<int32_t>(const std::string &prefix, size_t count);
template void GenerateUniformRandomNumbers<uint32_t>(const std::string &prefix, size_t count);
