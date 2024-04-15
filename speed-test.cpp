//
// Created by peter on 4/10/24.
//
#include "utils/unordered_file_writer.h"
#include "absl/log/log.h"
#include "utils/ordered_file_writer.h"
#include "parlay/random.h"

void UnorderedFileWriterTest() {
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << 40;
    const size_t SINGLE_WRITE_SIZE = 4 * (1UL << 20);
    UnorderedFileWriter<Type> writer(prefix, 4000, 4000, 5);
    size_t n = SINGLE_WRITE_SIZE / sizeof(Type);

    LOG(INFO) << "Preparing data";
    auto array = std::shared_ptr<Type>((Type*)malloc(SINGLE_WRITE_SIZE), free);
    for (Type i = 0; i < (Type)n; i++) {
        array.get()[i] = i * i - 5 * i - 1;
    }
    LOG(INFO) << "Starting writer loop";
    for (size_t i = 0; i < TOTAL_WRITE_SIZE / SINGLE_WRITE_SIZE; i++) {
        writer.Push(array, n);
    }
    writer.Close();
    LOG(INFO) << "Waiting for completion";
    writer.Wait();
    LOG(INFO) << "Done writing";
}

void OrderedFileWriterTest() {
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << 32;
    const size_t SINGLE_WRITE_SIZE = 4 * (1UL << 10);
    const size_t NUM_BUCKETS = 1000;
    OrderedFileWriter<Type> writer(prefix, NUM_BUCKETS, 4 * (1 << 20));
    const size_t n = SINGLE_WRITE_SIZE / sizeof(Type);
    LOG(INFO) << "Starting writer loop";
    parlay::random_generator gen;
    parlay::parallel_for(0, TOTAL_WRITE_SIZE / SINGLE_WRITE_SIZE, [&](size_t i) {
        std::uniform_int_distribution<size_t> dis(0, NUM_BUCKETS - 1);
        auto array = (Type*)malloc(SINGLE_WRITE_SIZE);
        for (Type index = 0; index < (Type)n; index++) {
            array[index] = index * index - 5 * index - 1;
        }
        auto r = gen[i];
        writer.Write(dis(r), array, n);
    });
    writer.CleanUp();
    LOG(INFO) << "Waiting for completion";
    auto results = writer.ReapResult();
    LOG(INFO) << "Done writing";
}

int main() {
    OrderedFileWriterTest();
}
