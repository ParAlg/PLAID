//
// Created by peter on 4/10/24.
//
#include "utils/unordered_file_writer.h"
#include "absl/log/log.h"
#include "utils/ordered_file_writer.h"
#include "parlay/random.h"
#include "utils/unordered_file_reader.h"

void UnorderedIOTest() {
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << 35;
    const size_t SINGLE_WRITE_SIZE = 4 * (1UL << 20);
    size_t n = SINGLE_WRITE_SIZE / sizeof(Type);
    std::shared_ptr<Type> array;
    {
        UnorderedFileWriter<Type> writer(prefix, 4000, 4000, 2);

        LOG(INFO) << "Preparing data";
        array = std::shared_ptr<Type>((Type *) malloc(SINGLE_WRITE_SIZE), free);
        for (Type i = 0; i < (Type) n; i++) {
            array.get()[i] = i * i - 5 * i - 1;
        }
        LOG(INFO) << "Starting writer loop";
        for (size_t i = 0; i < TOTAL_WRITE_SIZE / SINGLE_WRITE_SIZE; i++) {
            writer.Push(array, n);
        }
        writer.Close();
        LOG(INFO) << "Waiting for completion";
        writer.Wait();
    }
    LOG(INFO) << "Done writing. Now looking for these files.";

    auto files = FindFiles(prefix);
    LOG(INFO) << "Files found";
    UnorderedFileReader<Type> reader;
    reader.PrepFiles(files);
    reader.Start(n, 4096, 4096, 25);
    for (size_t i = 0; i < TOTAL_WRITE_SIZE / SINGLE_WRITE_SIZE; i++) {
        auto [ptr, size] = reader.Poll();
        if (size != n) {
            LOG(ERROR) << "Read size is expected to be the same";
            exit(0);
        }
        auto result = memcmp(ptr, array.get(), SINGLE_WRITE_SIZE);
        if (result != 0) {
            LOG(ERROR) << "Expected two arrays to be the same";
            exit(0);
        }
        free(ptr);
    }
    auto [ptr, size] = reader.Poll();
    if (ptr != nullptr || size != 0) {
        LOG(ERROR) << "Received more input than expected";
    }
    LOG(INFO) << "File reader completed";
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
        auto array = (Type *) malloc(SINGLE_WRITE_SIZE);
        for (Type index = 0; index < (Type) n; index++) {
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
    UnorderedIOTest();
}
