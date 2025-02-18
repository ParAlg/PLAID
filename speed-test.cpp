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
#include "utils/command_line.h"
#include "utils/random_read.h"
#include "utils/unordered_file_reader.h"
#include "benchmarks/io_benchmarks.h"
#include "utils/random_number_generator.h"
#include "benchmarks/in_memory_benchmarks.h"
#include "benchmarks/distribution_benchmarks.h"
#include <map>

void RandomReadTest(int argc, char **argv) {
    if (argc < 4) {
        std::cout << "Need arguments for number of elements and number of queries\n";
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

void AlignedAllocTest(int argc, char **argv) {
    parlay::internal::timer timer;
    const size_t NUM_ALLOCATIONS = 10000, SIZE = 4 << 20;
    for (size_t alignment: {64, 512, 4096}) {
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
        {"write_only",            UnorderedWriteTest},
        {"read_only",             UnorderedReadTest},
        {"ordered_writer",        OrderedFileWriterTest},
        {"rand_read",             RandomReadTest},
        {"large_read",            LargeReadTest},
        {"sorting_in_memory",     InMemorySortingTest},
        {"permutation_in_memory", InMemoryPermutationTest},
        {"reduce_in_memory",      InMemoryReduceTest},
        {"map_in_memory",         InMemoryMapTest},
        {"scatter_gather_nop",    ScatterGatherNopTest},
        {"scatter_gather_no_io",  ScatterGatherNoIOTest},
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
