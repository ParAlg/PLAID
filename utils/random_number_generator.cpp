//
// Created by peter on 3/6/24.
//

#include "random_number_generator.h"
#include "parlay/parallel.h"
#include "parlay/sequence.h"
#include "parlay/primitives.h"
#include "utils/unordered_file_writer.h"

#include <random>
#include <cstdlib>

template<typename T>
parlay::sequence<T> RandomSequence(size_t n, T max_num) {
    parlay::random_generator rng;
    auto limit = std::numeric_limits<T>();
    std::uniform_int_distribution<T> dist(limit.min(), n == 0 ? limit.max() : max_num);
    parlay::sequence<T> sequence(n);
    parlay::parallel_for(0, n, [&](size_t i) {
        auto r = rng[i];
        sequence[i] = dist(r);
    });
    return sequence;
}

template parlay::sequence<int64_t> RandomSequence(size_t, int64_t limit);

template parlay::sequence<uint64_t> RandomSequence(size_t, uint64_t limit);

template parlay::sequence<int32_t> RandomSequence(size_t, int32_t limit);

template parlay::sequence<uint32_t> RandomSequence(size_t, uint32_t limit);

template parlay::sequence<int8_t> RandomSequence(size_t, int8_t limit);

template parlay::sequence<uint8_t> RandomSequence(size_t, uint8_t limit);

/**
 * From https://github.com/ucrparlay/DovetailSort/blob/91eab06c2b3256104f7ac2b71227aae664e55e5a/include/parlay/generator.h
 * License: MIT
 */
template<typename T>
parlay::sequence<T> GenerateZipfianDistribution(size_t n, double s) {
    size_t cutoff = n;
    auto harmonic = parlay::delayed_seq<double>(cutoff, [&](size_t i) { return 1.0 / std::pow(i + 1, s); });
    double sum = parlay::reduce(make_slice(harmonic));
    double v = n / sum;
    parlay::sequence<size_t> nums(cutoff + 1, 0);
    parlay::parallel_for(0, cutoff, [&](size_t i) { nums[i] = (T) std::max(1.0, v / std::pow(i + 1, s)); });
    [[maybe_unused]]
    size_t tot = parlay::scan_inplace(make_slice(nums));
    assert(tot >= n);
    parlay::sequence<T> seq(n);
    parlay::parallel_for(0, cutoff, [&](size_t i) {
        parlay::parallel_for(nums[i], std::min(n, nums[i + 1]), [&](size_t j) {
            seq[j] = i;
        });
    });
    return random_shuffle(seq);
}

template<typename T>
parlay::sequence<T> GenerateExponentialDistribution(size_t count, double lambda) {
    parlay::random_generator random;
    std::exponential_distribution<double> distribution(lambda);

    return parlay::tabulate(count, [&](size_t i) {
        auto rng = random[i];
        return (T) distribution(rng);
    });
}

template<typename T>
void WriteNumbers(const std::string &prefix, size_t n, const T *data) {
    UnorderedFileWriter<T> writer(prefix);
    constexpr auto WRITE_SIZE = 4 << 20;
    const auto step = WRITE_SIZE / sizeof(T);
    for (size_t i = 0; i < n; i += step) {
        T *buffer = (T *) std::aligned_alloc(O_DIRECT_MULTIPLE, WRITE_SIZE);
        memcpy(buffer, data + i, WRITE_SIZE);
        std::shared_ptr<T> temp(buffer, free);
        writer.Push(temp, step);
    }
    writer.Close();
}

template<typename T>
void WriteNumbers(const std::string &prefix, size_t n, const std::function<parlay::sequence<T>(size_t)> &generator) {
    UnorderedFileWriter<T> writer(prefix);
    constexpr auto WRITE_SIZE = 4 << 20;
    const auto step = WRITE_SIZE / sizeof(T);
    for (size_t i = 0; i < n; i += step) {
        T *buffer = (T *) std::aligned_alloc(O_DIRECT_MULTIPLE, WRITE_SIZE);
        auto seq = generator(step);
        memcpy(buffer, seq.data(), WRITE_SIZE);
        std::shared_ptr<T> temp(buffer, free);
        writer.Push(temp, step);
    }
    writer.Close();
}

template<typename T>
void GenerateZipfianRandomNumbers(const std::string &prefix, size_t n, double s) {
    auto nums = GenerateZipfianDistribution<T>(n, s);
    T *data = nums.data();
    WriteNumbers(prefix, n, data);
}

template<typename T>
void GenerateExponentialRandomNumbers(const std::string &prefix, size_t n, double s) {
    WriteNumbers<T>(prefix, n, [&](size_t sz) {
        return GenerateExponentialDistribution<T>(sz, s);
    });
}

template<typename T>
void GenerateUniformRandomNumbers(const std::string &prefix, size_t count, T limit_max) {
    const size_t GRANULARITY = 1 << 20;
    UnorderedFileWriter<T> writer(prefix);
    CHECK(count % GRANULARITY == 0) << "Can only generate numbers that are a multiple of " << GRANULARITY;
    std::atomic<int64_t> num_blocks = (int64_t) (count / GRANULARITY);
    parlay::parallel_for(0, THREAD_COUNT, [&](size_t _) {
        std::random_device device;
        std::mt19937 rng(device());
        auto limit = std::numeric_limits<T>();
        if (limit_max == 0) {
            limit_max = limit.max();
        }
        std::uniform_int_distribution<T> distribution(limit.min(), limit_max);
        while (true) {
            auto current = num_blocks--;
            if (current <= 0) {
                return;
            }
            std::shared_ptr<T> result((T *) aligned_alloc(O_DIRECT_MULTIPLE, GRANULARITY * sizeof(T)), free);
            for (size_t i = 0; i < GRANULARITY; i++) {
                result.get()[i] = distribution(rng);
            }
            writer.Push(result, GRANULARITY);
        }
    }, 1);
    writer.Close();
}

template void GenerateUniformRandomNumbers<int64_t>(const std::string &prefix, size_t count, int64_t limit);

template void GenerateUniformRandomNumbers<uint64_t>(const std::string &prefix, size_t count, uint64_t limit);

template void GenerateUniformRandomNumbers<int32_t>(const std::string &prefix, size_t count, int32_t limit);

template void GenerateUniformRandomNumbers<uint32_t>(const std::string &prefix, size_t count, uint32_t limit);

template void GenerateZipfianRandomNumbers<size_t>(const std::string &prefix, size_t n, double s);

template void GenerateExponentialRandomNumbers<size_t>(const std::string &prefix, size_t n, double s);
