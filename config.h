//
// Created by peter on 3/1/24.
//

#ifndef SORTING_CONFIG_H
#define SORTING_CONFIG_H

#include <cstddef>

constexpr size_t MAIN_MEMORY_SIZE = 400ULL * (1 << 30);

constexpr size_t THREAD_COUNT = 128;

constexpr size_t SSD_COUNT= 30;
// (advisory) size of a single file, may go a little higher in practice
constexpr size_t FILE_SIZE = 1 << 30;

constexpr size_t IO_VECTOR_SIZE = 1024;

constexpr size_t O_DIRECT_MULTIPLE = 4096;
// It should never be necessary to change this unless O_DIRECT_MULTIPLE is very large
constexpr size_t METADATA_SIZE = 2;
constexpr size_t IO_URING_BUFFER_SIZE = 60;



#endif //SORTING_CONFIG_H
