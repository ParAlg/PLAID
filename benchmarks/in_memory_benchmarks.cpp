//
// Created by peter on 2/18/25.
//

#include "in_memory_benchmarks.h"

#include "utils/random_number_generator.h"
#include "parlay/primitives.h"
#include "absl/log/check.h"

const size_t THREAD_COUNT = parlay::num_workers();

void InMemorySortingTest(int argc, char **argv) {
    typedef size_t Type;
    CHECK(argc > 2) << "Expected number of elements to sort";
    const size_t n = 1UL << strtol(argv[2], nullptr, 10);
    parlay::internal::timer timer("sort");
    parlay::sequence<Type> array(n, 0);
    size_t step = n / THREAD_COUNT;
    using Limit = std::numeric_limits<unsigned long>;
    parlay::parallel_for(
            0, THREAD_COUNT,
            [&](size_t i) {
                size_t start = step * i, end = step * (i + 1);
                std::random_device device;
                std::mt19937 rng(device());
                std::uniform_int_distribution<Type> distribution(Limit::min(),
                                                                 Limit::max());
                for (size_t j = start; j < end; j++) {
                    array[j] = distribution(rng);
                }
            },
            1);
    timer.next("Start in-place sorting");
    parlay::sort_inplace(array);
    timer.next("parlay::sort_inplace DONE");
}

void InMemoryPermutationTest(int argc, char **argv) {
    typedef size_t Type;
    CHECK(argc > 2) << "Expected number of elements to permute";
    const size_t n = 1UL << strtol(argv[2], nullptr, 10);
    parlay::internal::timer timer("perm");
    parlay::sequence<Type> array(n, 0);
    size_t step = n / THREAD_COUNT;
    using Limit = std::numeric_limits<unsigned long>;
    parlay::parallel_for(
            0, THREAD_COUNT,
            [&](size_t i) {
                size_t start = step * i, end = step * (i + 1);
                std::random_device device;
                std::mt19937 rng(device());
                std::uniform_int_distribution<Type> distribution(Limit::min(),
                                                                 Limit::max());
                for (size_t j = start; j < end; j++) {
                    array[j] = distribution(rng);
                }
            },
            1);
    timer.next("Start permutation");
    parlay::random_shuffle(array);
    timer.next("parlay::permutation DONE");
}

void InMemoryReduceTest(int argc, char **argv) {
    using T = uint64_t;
    CHECK(argc > 2) << "Expected number of elements to reduce";
    const size_t n = 1UL << strtol(argv[2], nullptr, 10);
    auto sequence = RandomSequence<T>(n);
    parlay::internal::timer timer("In memory reduce");
    timer.next("Start reduce");
    parlay::monoid monoid([](T a, T b) { return a ^ b; }, 0);
    [[maybe_unused]] auto result = parlay::reduce(sequence, monoid);
    timer.next("Reduce done");
}

void InMemoryMapTest(int argc, char **argv) {
    using T = uint64_t;
    CHECK(argc > 2) << "Expected number of elements to map";
    const size_t n = 1UL << strtol(argv[2], nullptr, 10);
    auto sequence = RandomSequence<T>(n);
    parlay::internal::timer timer("In memory map");
    timer.next("Start map");
    [[maybe_unused]]
    auto result = parlay::map(sequence, [](T num) { return num / 2; });
    timer.next("Map done");
}
