//
// Created by peter on 4/10/24.
//
#include "utils/unordered_file_writer.h"
#include "absl/log/log.h"

int main() {
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
