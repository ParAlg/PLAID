//
// Created by peter on 3/6/24.
//

#include "random_number_generator.h"
#include "parlay/parallel.h"

#include <random>

template <typename T>
std::unique_ptr<PointerVector<T>> WriteUniformRandomNumbers(UnorderedFileWriter<T> *writer, size_t count, size_t granularity) {
    std::random_device device;
    std::mt19937 rng(device());
    auto limit = std::numeric_limits<T>();
    std::uniform_int_distribution<T> distribution(limit.min(), limit.max());
    auto result = std::make_unique<PointerVector<T>>();
    while (count > 0) {
        auto array_size = std::min(granularity, count);
        std::shared_ptr<T> array((T*)malloc(array_size * sizeof(T)), free);
        count -= array_size;
        for (size_t i = 0; i < array_size; i++) {
            array.get()[i] = distribution(rng);
        }
        writer->Push(array, array_size);
        result->emplace_back(std::move(array), array_size);
    }
    return result;
}

template <typename T>
PointerVector<T> GenerateUniformRandomNumbers(const std::string &prefix, size_t count, size_t nums_per_thread) {
    auto writer = std::make_unique<UnorderedFileWriter<T>>(prefix);
    if (nums_per_thread == 0) {
        // TODO: we should let parlay decide; is there a minimum granularity we want here?
        nums_per_thread = std::max(1UL << 20, count / 256);
    }
    nums_per_thread = std::min(nums_per_thread, count);
    std::vector<std::unique_ptr<PointerVector<T>>> result_vectors;
    std::mutex result_lock;
    parlay::parallel_for(0, count / nums_per_thread, [&](size_t i){
        auto r = WriteUniformRandomNumbers<T>(writer.get(), nums_per_thread, 1 << 20);
        std::lock_guard<std::mutex> guard(result_lock);
        result_vectors.push_back(std::move(r));
    });
    PointerVector<T> result;
    for (auto &vector : result_vectors) {
        for (auto &pair : *vector) {
            result.emplace_back(std::move(pair.first), pair.second);
        }
    }
    return result;
}

template PointerVector<int32_t> GenerateUniformRandomNumbers(const std::string &prefix, size_t count, size_t nums_per_thread);
PointerVector<int64_t> GenerateUniformRandomNumbers(const std::string &prefix, size_t count, size_t nums_per_thread);
