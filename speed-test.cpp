/**
 * Speed test for readers and writers.
 *
 * At the time of this commit, the unordered writer writes at 80GiB/s and reader
 * at 70GiB/s. The result seems good enough. It's not at fio's level, but should
 * be sufficient for our algorithm.
 */
#include "absl/log/log.h"
#include "parlay/primitives.h"
#include "parlay/random.h"
#include "scatter_gather_algorithms/nop.h"
#include "scatter_gather_algorithms/scatter_gather.h"
#include "utils/command_line.h"
#include "utils/ordered_file_writer.h"
#include "utils/random_read.h"
#include "utils/unordered_file_reader.h"
#include "utils/unordered_file_writer.h"
#include <map>

template<typename T>
void WriteFiles(const std::string &prefix, size_t n_bytes,
                size_t single_write_size = 4 * (1UL << 20)) {
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

void UnorderedIOTest(int argc, char **argv) {
    CHECK(argc > 2) << "Expected an argument on write size";
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << std::strtol(argv[2], nullptr, 10);
    const size_t SINGLE_WRITE_SIZE = 4 * (1UL << 20);
    size_t n = SINGLE_WRITE_SIZE / sizeof(Type);
    WriteFiles<Type>(prefix, TOTAL_WRITE_SIZE, SINGLE_WRITE_SIZE);
    LOG(INFO) << "Done writing. Now looking for these files.";

    auto files = FindFiles(prefix);
    LOG(INFO) << "Files found";
    UnorderedFileReader<Type> reader;
    reader.PrepFiles(files);
    reader.Start(n, 512, 512, 2);
    for (size_t i = 0; i < TOTAL_WRITE_SIZE / SINGLE_WRITE_SIZE; i++) {
        auto [ptr, size, _, _2] = reader.Poll();
        CHECK(size == n) << "Read size is expected to be the same. Actual size: "
                         << size;
        //        auto result = memcmp(ptr, array.get(), SINGLE_WRITE_SIZE);
        //        CHECK(result == 0) << "Expected two arrays to be the same";
        free(ptr);
    }
    auto [ptr, size, _, _2] = reader.Poll();
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
    LOG(INFO) << "Starting reading " << files.size() << " files " << expected_size
              << " bytes " << (expected_size >> 30) << " GiB";
    UnorderedFileReader<size_t> reader;
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
        expected_size -= size * sizeof(size_t);
    }
    CHECK(expected_size == 0)
                    << "Still " << expected_size << " bytes remaining unread";
    LOG(INFO) << "Done reading";
}

void OrderedFileWriterTest(int argc, char **argv) {
    struct WriterData {
        unsigned char data[SAMPLE_SORT_BUCKET_SIZE];
    };
    using Allocator = AlignedTypeAllocator<WriterData, O_DIRECT_MULTIPLE>;

    CHECK(argc > 3)
                    << "Expected an argument on total write size and number of buckets";
    using Type = long long;
    const std::string prefix = "test_files";
    const size_t TOTAL_WRITE_SIZE = 1UL << std::strtol(argv[2], nullptr, 10);
    const size_t SINGLE_WRITE_SIZE = SAMPLE_SORT_BUCKET_SIZE;
    const size_t NUM_BUCKETS = std::strtol(argv[3], nullptr, 10);
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

void InMemorySortingTest(int argc, char **argv) {
    typedef size_t Type;
    CHECK(argc > 2) << "Expected number of elements to sort";
    const size_t n = 1UL << std::strtol(argv[2], nullptr, 10);
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
    const size_t n = 1UL << std::strtol(argv[2], nullptr, 10);
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

template<typename T>
parlay::sequence<T> RandomSequence(size_t n) {
    parlay::random_generator rng;
    std::uniform_int_distribution<T> dist;
    parlay::sequence<T> sequence(n);
    parlay::parallel_for(0, n, [&](size_t i) {
        auto r = rng[i];
        sequence[i] = dist(r);
    });
    return sequence;
}

void InMemoryReduceTest(int argc, char **argv) {
    using T = uint64_t;
    CHECK(argc > 2) << "Expected number of elements to reduce";
    const size_t n = 1UL << std::strtol(argv[2], nullptr, 10);
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
    const size_t n = 1UL << std::strtol(argv[2], nullptr, 10);
    auto sequence = RandomSequence<T>(n);
    parlay::internal::timer timer("In memory map");
    timer.next("Start map");
    auto result = parlay::map(sequence, [](T num) { return num / 2; });
    timer.next("Map done");
}

void RandomReadTest(int argc, char **argv) {
    if (argc < 4) {
        std::cout
                << "Need arguments for number of elements and number of queries\n";
        return;
    }
    size_t n = 1UL << std::strtol(argv[2], nullptr, 10),
            m = 1UL << std::strtol(argv[3], nullptr, 10);
    parlay::internal::timer timer("random read");

    std::random_device rd;
    auto file_name = GetFileName("random_read_test", rd());
    LOG(INFO) << "Using " << file_name;
    size_t buffer_size = sizeof(size_t) * n;
    auto *buffer =
            static_cast<size_t *>(aligned_alloc(O_DIRECT_MULTIPLE, buffer_size));
    for (size_t i = 0; i < n; i++) {
        buffer[i] = i;
    }
    int fd = open(file_name.c_str(), O_WRONLY | O_CREAT | O_DIRECT, 0744);
    size_t bytes_written = 0;
    while (bytes_written < buffer_size) {
        size_t result = write(fd, (unsigned char *) buffer + bytes_written,
                              buffer_size - bytes_written);
        SYSCALL(result);
        bytes_written += result;
    }
    close(fd);
    free(buffer);

    timer.next("File setup complete.");

    parlay::random_generator gen;
    std::uniform_int_distribution<size_t> dis(0, n - 1);
    parlay::sequence<size_t> queries =
            parlay::map(parlay::iota(m), [&](size_t i) {
                auto g = gen[i];
                return dis(g);
            });
    timer.next("Begin random read");
    auto result = RandomBatchRead<size_t>(
            {FileInfo(file_name, 0, buffer_size, buffer_size)}, queries);
    timer.next("Random read completed");
    std::sort(result.begin(), result.end());
    auto expected = parlay::sort(queries);
    size_t num_mismatches = 0;
    for (size_t i = 0; i < m; i++) {
        if (result[i] != expected[i]) {
            LOG(ERROR) << "Mismatch at index " << i << ": expected " << expected[i]
                       << ", found " << result[i];
            num_mismatches++;
            if (num_mismatches >= 20) {
                break;
            }
        }
    }
}

void LargeReadTest(int argc, char **argv) {
    using T = size_t;
    constexpr auto NUM_IO_VECTORS = 16;
    struct Buffer {
        T *buffer;
        iovec io_vectors[NUM_IO_VECTORS];

        explicit Buffer(size_t size) {
            buffer = (T *) aligned_alloc(O_DIRECT_MULTIPLE, size);
            for (size_t i = 0; i < NUM_IO_VECTORS; i++) {
                char *ptr = (char *) buffer;
                io_vectors[i].iov_base = ptr + (size / NUM_IO_VECTORS) * i;
                io_vectors[i].iov_len = size / NUM_IO_VECTORS;
            }
        }

        ~Buffer() { free(buffer); }
    };

    CHECK(argc >= 5);
    std::string prefix(argv[2]);
    auto files = FindFiles(prefix);
    size_t read_size = 1UL << ParseLong(argv[3]);
    LOG(INFO) << "Read size: " << read_size / (1 << 20) << " MiB";
    // WriteFiles<size_t>(prefix, total_size);
    CHECK(!files.empty());
    GetFileInfo(files);
    std::vector<int> fds;
    for (auto &file: files) {
        fds.push_back(open(file.file_name.c_str(), O_RDONLY | O_DIRECT));
    }

    size_t max_requests_in_ring = ParseLong(argv[4]);
    size_t buffer_size = read_size * max_requests_in_ring;
    std::queue<Buffer *> buffers;
    for (size_t i = 0; i < buffer_size; i += read_size) {
        auto buffer = new Buffer(read_size);
        buffers.push(buffer);
    }

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> file_distribution(0, fds.size() - 1);

    struct io_uring ring;
    io_uring_queue_init(max_requests_in_ring, &ring, IORING_SETUP_SINGLE_ISSUER);
    size_t requests_in_ring = 0;
    while (true) {
        while (requests_in_ring < max_requests_in_ring) {
            auto sqe = io_uring_get_sqe(&ring);
            SYSCALL((ptrdiff_t) sqe);
            size_t file_index = file_distribution(rng);
            FileInfo file = files[file_index];
            auto buffer = buffers.front();
            buffers.pop();
            std::uniform_int_distribution<size_t> offset_distribution(
                    0, file.file_size / read_size - 1);
            io_uring_prep_readv(sqe, fds[file_index], buffer->io_vectors,
                                NUM_IO_VECTORS, offset_distribution(rng) * read_size);
            io_uring_sqe_set_data(sqe, buffer);
            requests_in_ring++;
        }
        io_uring_submit(&ring);
        while (requests_in_ring > 0) {
            struct io_uring_cqe *cqe;
            if (requests_in_ring == max_requests_in_ring) {
                SYSCALL(io_uring_wait_cqe(&ring, &cqe));
            } else {
                int res = io_uring_peek_cqe(&ring, &cqe);
                if (res != 0) {
                    break;
                }
            }
            SYSCALL(cqe->res);
            requests_in_ring--;
            io_uring_cqe_seen(&ring, cqe);
            buffers.push((Buffer *) io_uring_cqe_get_data(cqe));
        }
    }
}

void ScatterGatherNopTest(int argc, char **argv) {
    CHECK(argc == 4) << "Usage: " << argv[0] << " " << argv[1]
                     << " <prefix> <bucket size>";
    std::string input_prefix(argv[2]);
    int num_buckets = (int) ParseLong(argv[3]);
    ScatterGatherNop<size_t>(input_prefix, num_buckets);
}

void AlignedAllocTest(int argc, char **argv) {
    parlay::internal::timer timer;
    const size_t NUM_ALLOCATIONS = 10000, SIZE = 4 << 20;
    for (size_t alignment : {64, 512, 4096}) {
        LOG(INFO) << "Using alignment " << alignment;
        for (int rep = 0; rep < 3; rep++) {
            std::vector<void *> pointers(NUM_ALLOCATIONS, nullptr);
            timer.next("Starting aligned alloc");
            for (size_t i = 0; i < NUM_ALLOCATIONS; i++) {
                pointers.push_back(std::aligned_alloc(alignment, SIZE));
            }
            timer.next("Aligned alloc took");
            std::for_each(pointers.begin(), pointers.end(), free);
            pointers.clear();
            timer.next("Allocating with malloc");
            for (size_t i = 0; i < NUM_ALLOCATIONS; i++) {
                pointers.push_back(malloc(SIZE));
            }
            timer.next("Malloc took");
            std::for_each(pointers.begin(), pointers.end(), free);
            timer.next("Done");
        }
    }
}

std::map<std::string, std::function<void(int, char **)>> test_functions = {
        {"unordered_io",          UnorderedIOTest},
        {"read_only",             ReadOnlyTest},
        {"ordered_writer",        OrderedFileWriterTest},
        {"rand_read",             RandomReadTest},
        {"large_read",            LargeReadTest},
        {"sorting_in_memory",     InMemorySortingTest},
        {"permutation_in_memory", InMemoryPermutationTest},
        {"reduce_in_memory",      InMemoryReduceTest},
        {"scatter_gather_nop",    ScatterGatherNopTest},
        {"map_in_memory",         InMemoryMapTest},
        {"aligned_alloc",         AlignedAllocTest}};

int main(int argc, char **argv) {
    ParseGlobalArguments(argc, argv);
    if (argc < 2) {
        usage:
        std::cout << "Usage: " << argv[0] << " "
                  << "<which test to perform> <test-specific arguments>\n";
        return 0;
    }
    std::string test_name(argv[1]);
    if (test_functions.count(test_name) == 0) {
        goto usage;
    }
    std::cout << "Performing " << test_name << "\n";
    test_functions[test_name](argc, argv);
}
