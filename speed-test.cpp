/**
 * Speed test for readers and writers.
 *
 * At the time of this commit, the unordered writer writes at 80GiB/s and reader at 70GiB/s.
 * The result seems good enough. It's not at fio's level, but should be sufficient for our algorithm.
 */
#include <map>
#include "utils/unordered_file_writer.h"
#include "absl/log/log.h"
#include "utils/ordered_file_writer.h"
#include "parlay/random.h"
#include "parlay/primitives.h"
#include "utils/unordered_file_reader.h"
#include "utils/random_read.h"

void UnorderedIOTest(int argc, char **argv) {
    CHECK(argc > 2) << "Expected an argument on write size";
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << std::strtol(argv[2], nullptr, 10);
    const size_t SINGLE_WRITE_SIZE = 4 * (1UL << 20);
    size_t n = SINGLE_WRITE_SIZE / sizeof(Type);
    std::shared_ptr<Type> array;
    {
        UnorderedFileWriter<Type> writer(prefix, 128, 2);

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
    reader.Start(n, 512, 512, 2);
    for (size_t i = 0; i < TOTAL_WRITE_SIZE / SINGLE_WRITE_SIZE; i++) {
        auto [ptr, size] = reader.Poll();
        CHECK(size == n) << "Read size is expected to be the same. Actual size: " << size;
//        auto result = memcmp(ptr, array.get(), SINGLE_WRITE_SIZE);
//        CHECK(result == 0) << "Expected two arrays to be the same";
        free(ptr);
    }
    auto [ptr, size] = reader.Poll();
    if (ptr != nullptr || size != 0) {
        LOG(ERROR) << "Received more input than expected";
    }
    LOG(INFO) << "File reader completed";
}

void ReadOnlyTest(int argc, char **argv) {
    auto files = FindFiles("test_files");
    size_t expected_size = 0;
    for (auto &file: files) {
        expected_size += file.file_size;
    }
    LOG(INFO) << "Starting reading " << files.size() << " files "
              << expected_size << " bytes "
              << (expected_size >> 30) << " GiB";
    UnorderedFileReader<size_t> reader;
    reader.PrepFiles(files);
    reader.SetBufferQueueSize(512);
    // FIXME: allocator is the bottleneck.
    //  jemalloc performs worse than glibc (1/3) while mimalloc is much faster.
    reader.Start(1 << 20, 128, 128, 4);
    while (true) {
        auto [ptr, size] = reader.Poll();
        if (ptr == nullptr || size == 0) {
            break;
        }
        free(ptr);
        expected_size -= size * sizeof(size_t);
    }
    CHECK(expected_size == 0) << "Still " << expected_size << " bytes remaining unread";
    LOG(INFO) << "Done reading";
}

void OrderedFileWriterTest(int argc, char **argv) {
    CHECK(argc > 3) << "Expected an argument on total write size and number of buckets";
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << std::strtol(argv[2], nullptr, 10);
    const size_t SINGLE_WRITE_SIZE = 4 * (1UL << 10);
    const size_t NUM_BUCKETS = std::strtol(argv[3], nullptr, 10);
    OrderedFileWriter<Type, SAMPLE_SORT_BUCKET_SIZE> writer(prefix, NUM_BUCKETS, 4 * (1 << 20));
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

void InMemorySortingTest(int argc, char **argv) {
    CHECK(argc > 2) << "Expected number of elements to sort";
    const size_t n = 1UL << std::strtol(argv[2], nullptr, 10);
    parlay::internal::timer timer("sort");
    auto array = parlay::random_permutation(n);
    timer.next("Permutation done");
    auto res = parlay::sort(array);
    timer.next("Sorting done");
}

void RandomReadTest(int argc, char **argv) {
    if (argc < 4) {
        std::cout << "Need arguments for number of elements and number of queries\n";
        return;
    }
    size_t n = 1UL << std::strtol(argv[2], nullptr, 10), m = 1UL << std::strtol(argv[3], nullptr, 10);
    parlay::internal::timer timer("random read");

    std::random_device rd;
    auto file_name = GetFileName("random_read_test", rd());
    LOG(INFO) << "Using " << file_name;
    size_t buffer_size = sizeof(size_t) * n;
    auto *buffer = static_cast<size_t *>(malloc(buffer_size));
    for (size_t i = 0; i < n; i++) {
        buffer[i] = i;
    }
    int fd = open(file_name.c_str(), O_WRONLY | O_CREAT | O_DIRECT, 0744);
    size_t bytes_written = 0;
    while (bytes_written < buffer_size) {
        size_t result = write(fd, (unsigned char *)buffer + bytes_written, buffer_size - bytes_written);
        SYSCALL(result);
        bytes_written += result;
    }
    close(fd);
    free(buffer);

    timer.next("File setup complete.");

    parlay::random_generator gen;
    std::uniform_int_distribution<size_t> dis(0, n-1);
    parlay::sequence<size_t> queries = parlay::map(parlay::iota(m), [&](size_t i) {
        auto g = gen[i];
        return dis(g);
    });
    timer.next("Begin random read");
    auto result = RandomBatchRead<size_t>({FileInfo(file_name, buffer_size, buffer_size)}, queries);
    timer.next("Random read completed");
    std::sort(result.begin(), result.end());
    auto expected = parlay::sort(queries);
    size_t num_mismatches = 0;
    for (size_t i = 0; i < m; i++) {
        if (result[i] != expected[i]) {
            LOG(ERROR) << "Mismatch at index " << i << ": expected " << expected[i] << ", found " << result[i];
            num_mismatches++;
            if (num_mismatches >= 20) {
                break;
            }
        }
    }
}

std::map<std::string, std::function<void(int, char **)>> test_functions = {
    {"unordered_io", UnorderedIOTest},
    {"read_only", ReadOnlyTest},
    {"ordered_writer", OrderedFileWriterTest},
    {"sorting", InMemorySortingTest},
    {"rand_read", RandomReadTest}};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " " << "<which test to perform> <test-specific arguments>\n";
        return 0;
    }
    std::string test_name(argv[1]);
    std::cout << "Performing " << test_name << "\n";
    test_functions[test_name](argc, argv);
}
