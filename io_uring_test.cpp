#include <cstdlib>
#include <fcntl.h>
#include <liburing.h>
#include <cstring>
#include <sys/utsname.h>
#include <set>

#include "parlay/internal/get_time.h"

#include "utils/logger.h"
#include "utils/file_utils.h"

bool simple_write_test(const char* file_name, const void* data, int data_size) {
    bool success = true;
    LOG(INFO) << "Testing write to " << file_name;
    int fd = open(file_name, O_WRONLY | O_CREAT | O_DIRECT, 0744);
    SYSCALL(fd);
    auto res = write(fd, data, data_size);
    SYSCALL(res);
    if (res != data_size) {
        LOG(ERROR) << "Expected write of size " << data_size << ", got write of size " << res;
    }
    SYSCALL(close(fd));
    fd = open(file_name, O_RDONLY | O_DIRECT);
    void* buffer = malloc(data_size);
    res = read(fd, buffer, data_size);
    SYSCALL(res);
    if (res != data_size) {
        LOG(ERROR) << "Expected " << data_size << " bytes read. Got " << res << " bytes.";
        success = false;
    }
    if (memcmp(buffer, data, data_size) != 0) {
        LOG(ERROR) << "File content does not match.";
        success = false;
    }
    free(buffer);
    SYSCALL(close(fd));
    SYSCALL(unlink(file_name));
    return success;
}

bool io_uring_usable() {
    struct utsname u;
    uname(&u);
    LOG(INFO) << "Running on kernel version " << u.release;
    struct io_uring ring;
    int res = io_uring_queue_init(5, &ring, 0);
    if (res == 0) {
        io_uring_queue_exit(&ring);
        LOG(INFO) << "io_uring available";
        return true;
    }
    LOG(ERROR) << std::strerror(-res);
    LOG(ERROR) << "io_uring unavailable";
    return false;
}

void test_io_uring(const char* file_name, int* data, size_t array_size, size_t file_size) {
    parlay::internal::timer timer("test_io_uring");
    int fd = open(file_name, O_WRONLY | O_CREAT | O_DIRECT, 0744);
    SYSCALL(fd);

    struct io_uring ring;
    SYSCALL(io_uring_queue_init(3600, &ring, IORING_SETUP_SINGLE_ISSUER));

    std::set<size_t> expected_data;
    timer.next("Start writing");
    for (size_t i = 0; i < file_size / array_size; i++) {
        struct io_uring_sqe *sqe;
        do {
            sqe = io_uring_get_sqe(&ring);
        } while (sqe == nullptr);
        int *write_data = (int*)malloc(array_size);
        memcpy(write_data, data, array_size);
        write_data[0] = (int)i + 1;
        io_uring_prep_write(sqe, fd, write_data, array_size, i * array_size);
        io_uring_sqe_set_data(sqe, (void*)(i + 1));
        expected_data.insert(i + 1);
        SYSCALL(io_uring_submit(&ring));
    }
    timer.next("Wait for writes");
    for (size_t i = 0; i < file_size / array_size; i++) {
        struct io_uring_cqe *cqe;
        SYSCALL(io_uring_wait_cqe(&ring, &cqe));
        SYSCALL(cqe->res);
        auto index = (size_t)io_uring_cqe_get_data(cqe);
        if (expected_data.erase(index) != 1) {
            LOG(ERROR) << "Expected " << index << " in the set of ring data.";
        }
        io_uring_cqe_seen(&ring, cqe);
    }
    timer.next("All writes completed");
    SYSCALL(close(fd));
    fd = open(file_name, O_RDONLY | O_DIRECT);
    SYSCALL(fd);
    timer.next("Start reading");
    int **buffers = (int**)malloc(file_size / array_size * sizeof(int*));
    for (size_t i = 0; i < file_size / array_size; i++) {
        struct io_uring_sqe *sqe;
        do {
            sqe = io_uring_get_sqe(&ring);
        } while (sqe == nullptr);
        int *read_buffer = (int*)malloc(array_size);
        buffers[i] = read_buffer;
        io_uring_prep_read(sqe, fd, read_buffer, array_size, i * array_size);
        io_uring_sqe_set_data(sqe, (void*)i);
        SYSCALL(io_uring_submit(&ring));
    }
    timer.next("Waiting for reads");
    for (size_t i = 0; i < file_size / array_size; i++) {
        struct io_uring_cqe *cqe;
        SYSCALL(io_uring_wait_cqe(&ring, &cqe));
        SYSCALL(cqe->res);
        auto index = (size_t)io_uring_cqe_get_data(cqe);
        data[0] = (int)index + 1;
        if (memcmp(data, buffers[index], array_size) != 0) {
            LOG(ERROR) << "Data mismatch";
        }
    }
    timer.next("Reads completed");
    free(buffers);
    io_uring_queue_exit(&ring);
    SYSCALL(close(fd));
    SYSCALL(unlink(file_name));
}

int main() {
    LOG(INFO) << "Test setup";
    const size_t FILE_SIZE = 1UL << 28;
    const size_t N = 1UL << 20;
    const size_t ARRAY_SIZE = N * sizeof(int);
    int *data = (int*)malloc(ARRAY_SIZE);
    for (size_t j = 0; j < N; j++) {
        data[j] = (int)(j * j) - 3;
    }
    LOG(INFO) << "Testing whether a file can be written to the current directory using O_DIRECT.";
    CHECK(simple_write_test("./io_test", data, ARRAY_SIZE));
    LOG(INFO) << "Testing whether a file can be written to all SSDs.";
    PopulateSSDList();
    LOG(INFO) << "Promised " << SSD_COUNT << " SSDs in config file. Testing each of them.";
    for (const auto& ssd_name : GetSSDList()) {
        CHECK(simple_write_test((ssd_name + "/io_test").c_str(), data, ARRAY_SIZE));
    }
    LOG(INFO) << "Testing whether io_uring can be used";
    CHECK(io_uring_usable());
    LOG(INFO) << "Testing whether io_uring can be used to write to a SSD (small write)";
    test_io_uring((GetSSDList()[0] + "/io_uring_test").c_str(), data, ARRAY_SIZE, ARRAY_SIZE);
    LOG(INFO) << "Testing whether io_uring can be used to write to a SSD (big write)";
    test_io_uring((GetSSDList()[0] + "/io_uring_test").c_str(), data, ARRAY_SIZE, FILE_SIZE);
    LOG(INFO) << "All tests complete.";
    return 0;
}
