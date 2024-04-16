#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <liburing.h>
#include <cerrno>
#include <cstring>

#include "parlay/internal/get_time.h"

#include "utils/logger.h"

/**
 * Result: concurrently writing to and reading from a file yields about 5.7GB/s write speed and 7GB/s read speed
 */

int main() {
    const size_t FILE_SIZE = 1UL << 36;
    const size_t N = 1UL << 20;
    const size_t ARRAY_SIZE = N * sizeof(int);
    int *data = (int*)malloc(ARRAY_SIZE);
    for (size_t i = 0; i < N; i++) {
        data[i] = i * i - 3;
    }
    parlay::internal::timer timer("io_uring_test");

    const char* file_name = "/mnt/ssd7/io_test";
    int fd = open(file_name, O_WRONLY | O_CREAT | O_DIRECT, 0744);
    SYSCALL(fd);

    struct io_uring ring;
    SYSCALL(io_uring_queue_init(3600, &ring, IORING_SETUP_SQPOLL));

    timer.next("Start writing");
    for (size_t i = 0; i < FILE_SIZE / ARRAY_SIZE; i++) {
        struct io_uring_sqe *sqe;
        do {
            sqe = io_uring_get_sqe(&ring);
        } while (sqe == nullptr);
        io_uring_prep_write(sqe, fd, data, ARRAY_SIZE, i * ARRAY_SIZE);
        io_uring_sqe_set_data(sqe, (void*)(i + 1));
        SYSCALL(io_uring_submit(&ring));
    }
    timer.next("Wait for writes");
    for (size_t i = 0; i < FILE_SIZE / ARRAY_SIZE; i++) {
        struct io_uring_cqe *cqe;
        SYSCALL(io_uring_wait_cqe(&ring, &cqe));
        SYSCALL(cqe->res);
        std::cout << (size_t)io_uring_cqe_get_data(cqe) << ' ';
        io_uring_cqe_seen(&ring, cqe);
    }
    timer.next("All writes completed");
    SYSCALL(close(fd));
    fd = open(file_name, O_RDONLY | O_DIRECT);
    SYSCALL(fd);
    timer.next("Start reading");
    for (size_t i = 0; i < FILE_SIZE / ARRAY_SIZE; i++) {
        struct io_uring_sqe *sqe;
        do {
            sqe = io_uring_get_sqe(&ring);
        } while (sqe == nullptr);
        int *read_buffer = (int*)malloc(ARRAY_SIZE);
        io_uring_prep_read(sqe, fd, read_buffer, ARRAY_SIZE, i * ARRAY_SIZE);
        io_uring_sqe_set_data(sqe, read_buffer);
        SYSCALL(io_uring_submit(&ring));
    }
    timer.next("Waiting for reads");
    for (size_t i = 0; i < FILE_SIZE / ARRAY_SIZE; i++) {
        struct io_uring_cqe *cqe;
        SYSCALL(io_uring_wait_cqe(&ring, &cqe));
        SYSCALL(cqe->res);
        if (memcmp(data, io_uring_cqe_get_data(cqe), ARRAY_SIZE) != 0) {
            std::cerr << "Data mismatch" << std::endl;
        }
    }
    timer.next("Reads completed");
    io_uring_queue_exit(&ring);
    SYSCALL(close(fd));
}
