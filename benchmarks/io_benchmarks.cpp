//
// Created by peter on 2/18/25.
//

#include "benchmarks/io_benchmarks.h"

#include <map>
#include "utils/unordered_file_writer.h"
#include "utils/unordered_file_reader.h"
#include "utils/ordered_file_writer.h"
#include "utils/command_line.h"
#include "scatter_gather_algorithms/nop.h"
#include "parlay/random.h"
#include "parlay/primitives.h"
#include "absl/log/log.h"

const size_t SINGLE_IO_SIZE = 4 * (1UL << 20);

template<typename T>
void WriteFiles(const std::string &prefix, size_t n_bytes, size_t single_write_size) {
    UnorderedFileWriter<T> writer(prefix, 128, 2);
    size_t total_write_size = n_bytes;
    LOG(INFO) << "Preparing data";
    auto array = std::shared_ptr<T>(
            (T *) aligned_alloc(O_DIRECT_MULTIPLE, single_write_size), free);
    for (size_t i = 0; i < single_write_size / sizeof(T); i++) {
        array.get()[i] = i * i - 5 * i - 1;
    }
    LOG(INFO) << "Starting writer loop";
    for (size_t i = 0; i < total_write_size / single_write_size; i++) {
        writer.Push(array, single_write_size / sizeof(T));
    }
    LOG(INFO) << "Waiting for completion";
    writer.Wait();
    LOG(INFO) << "Files written";
}

void OrderedFileWriterTest(int argc, char **argv) {
    struct WriterData {
        unsigned char data[SAMPLE_SORT_BUCKET_SIZE];
    };
    using Allocator = AlignedTypeAllocator<WriterData, O_DIRECT_MULTIPLE>;

    CHECK(argc > 3) << "Expected an argument on total write size and number of buckets";
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << strtol(argv[2], nullptr, 10);
    const size_t SINGLE_WRITE_SIZE = SAMPLE_SORT_BUCKET_SIZE;
    const size_t NUM_BUCKETS = strtol(argv[3], nullptr, 10);
    OrderedFileWriter<Type, SAMPLE_SORT_BUCKET_SIZE> writer(prefix, NUM_BUCKETS,
                                                            4 * (1 << 20));
    parlay::par_do(
            [&]() { writer.RunIOThread(&writer); },
            [&]() {
                const size_t n = SINGLE_WRITE_SIZE / sizeof(Type);
                LOG(INFO) << "Starting writer loop";
                parlay::random_generator gen;
                parlay::parallel_for(
                        0, TOTAL_WRITE_SIZE / SINGLE_WRITE_SIZE, [&](size_t i) {
                            std::uniform_int_distribution<size_t> dis(0, NUM_BUCKETS - 1);
                            auto array = reinterpret_cast<Type *>(Allocator::alloc());
                            for (size_t index = 0; index < n; index++) {
                                array[index] = (Type) (index * index - 5 * index - 1);
                            }
                            auto r = gen[i];
                            writer.Write(dis(r), array, n);
                        });
                writer.CleanUp();
                LOG(INFO) << "Waiting for completion";
            });
    auto results = writer.ReapResult();
    LOG(INFO) << "Done writing";
}

void UnorderedReadTest(int argc, char **argv) {
    CHECK(argc > 3) << "Usage: " << argv[0] << " " << argv[1] << " <file prefix>";
    using Type = long long;
    parlay::internal::timer timer("Unordered read");
    auto files = FindFiles(std::string(argv[2]));
    size_t expected_size = 0;
    for (auto &file: files) {
        expected_size += file.file_size;
    }
    LOG(INFO) << "Starting reading " << files.size() << " files " << expected_size
              << " bytes " << (expected_size >> 30) << " GiB";
    timer.next("start benchmark");
    UnorderedFileReader<Type> reader;
    reader.PrepFiles(files);
    // FIXME: allocator is the bottleneck.
    //  jemalloc performs worse than glibc (1/3) while mimalloc is much faster.
    reader.Start(1 << 20, 128, 128, 4);
    while (true) {
        auto [ptr, size, _, _2] = reader.Poll();
        if (ptr == nullptr || size == 0) {
            break;
        }
        free(ptr);
        expected_size -= size * sizeof(Type);
    }
    CHECK(expected_size == 0)
                    << "Still " << expected_size << " bytes remaining unread";
    timer.next("DONE");
}

void UnorderedWriteTest(int argc, char **argv) {
    CHECK(argc > 3) << "Usage: " << argv[0] << " " << argv[1] << " <file prefix> <size (pow of 2)>";
    using Type = long long;
    const std::string prefix(argv[2]);
    const size_t TOTAL_WRITE_SIZE = 1UL << ParseLong(argv[3]);
    parlay::internal::timer timer("Unordered write");
    timer.next("experiment start");
    WriteFiles<Type>(prefix, TOTAL_WRITE_SIZE, SINGLE_IO_SIZE);
    timer.next("DONE");
}

