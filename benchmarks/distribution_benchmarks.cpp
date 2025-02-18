//
// Created by peter on 2/18/25.
//
#include "utils/command_line.h"
#include "absl/log/check.h"
#include "scatter_gather_algorithms/nop.h"
#include "utils/random_number_generator.h"

void ScatterGatherNopTest(int argc, char **argv) {
    CHECK(argc == 4) << "Usage: " << argv[0] << " " << argv[1]
                     << " <prefix> <bucket size>";
    std::string input_prefix(argv[2]);
    int num_buckets = (int) ParseLong(argv[3]);
    ScatterGatherNop<size_t>(input_prefix, num_buckets);
}

template<typename T>
void ScatterGatherThread(size_t num_buckets, const std::function<std::pair<T *, size_t>()> &f) {
    struct BucketData {
        char a[SAMPLE_SORT_BUCKET_SIZE];
    };
    using bucket_allocator = AlignedTypeAllocator<BucketData, O_DIRECT_MULTIPLE>;
    size_t buffer_size = SAMPLE_SORT_BUCKET_SIZE / sizeof(T);
    // each bucket stores a pointer to an array, which will hold temporary values in that bucket
    T *buckets[num_buckets];
    unsigned int buffer_index[num_buckets];
    for (size_t i = 0; i < num_buckets; i++) {
        buckets[i] = (T *) bucket_allocator::alloc();
        buffer_index[i] = 0;
    }
    while (true) {
        auto [data, size] = f();
        if (data == nullptr) {
            break;
        }
        for (size_t i = 0; i < size; i++) {
            // simply use mod to assign buckets
            size_t bucket_index = (data[i] + i) % num_buckets;
            buckets[bucket_index][buffer_index[bucket_index]++] = data[i];
            if (buffer_index[bucket_index] == buffer_size) {
                buffer_index[bucket_index] = 0;
                bucket_allocator::free((BucketData *) buckets[bucket_index]);
                buckets[bucket_index] = (T *) bucket_allocator::alloc();
            }
        }
        free(data);
    }
}

void ScatterGatherNoIOTest(int argc, char **argv) {
    using T = size_t;
    size_t size = 1ULL << ParseLong(argv[2]);
    size_t num_buckets = ParseLong(argv[3]);
    size_t n = size / sizeof(T);
    size_t num_elements_per_pointer = 1 << 20;
    size_t num_pointers = n / num_elements_per_pointer;
    size_t size_per_pointer = size / num_pointers;
    T *pointers[num_pointers];
    parlay::internal::timer timer("Scatter gather no IO");
    parlay::parallel_for(0, num_pointers, [&](size_t i) {
        auto seq = RandomSequence<T>(num_elements_per_pointer);
        pointers[i] = (T *) aligned_alloc(O_DIRECT_MULTIPLE, size_per_pointer);
        memcpy(pointers[i], seq.data(), size_per_pointer);
    });
    size_t cur = 0;
    std::mutex mutex;
    const auto generator = [&]() -> std::pair<T *, size_t> {
        mutex.lock();
        if (cur == num_pointers) {
            mutex.unlock();
            return {nullptr, 0};
        }
        T *result = pointers[cur];
        cur++;
        mutex.unlock();
        return {result, num_elements_per_pointer};
    };
    timer.next("Preparations complete");
    parlay::parallel_for(0, parlay::num_workers(), [&](size_t i) {
        ScatterGatherThread<T>(num_buckets, generator);
    }, 1);
    timer.next("All done");
}
