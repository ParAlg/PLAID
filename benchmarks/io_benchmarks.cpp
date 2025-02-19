//
// Created by peter on 2/18/25.
//

#include "benchmarks/io_benchmarks.h"

#include <map>
#include "utils/unordered_file_writer.h"
#include "utils/unordered_file_reader.h"
#include "utils/ordered_file_writer.h"
#include "utils/command_line.h"
#include "parlay/random.h"
#include "parlay/primitives.h"
#include "absl/log/log.h"
#include "utils/random_read.h"

const size_t SINGLE_IO_SIZE = 4 * (1UL << 20);

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
    UnorderedFileWriter<Type> writer(prefix, 64, 2);
    timer.next("preparing writes");
    size_t totalWriteSize = TOTAL_WRITE_SIZE;
    auto array = std::shared_ptr<Type>(
            (Type *) aligned_alloc(O_DIRECT_MULTIPLE, SINGLE_IO_SIZE), free);
    for (size_t i = 0; i < SINGLE_IO_SIZE / sizeof(Type); i++) {
        array.get()[i] = (Type)(i * i - 5 * i - 1);
    }
    timer.next("starting to write");
    for (size_t i = 0; i < totalWriteSize / SINGLE_IO_SIZE; i++) {
        writer.Push(array, SINGLE_IO_SIZE / sizeof(Type));
    }
    writer.Wait();
    timer.next("DONE");
}

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

