//
// Created by peter on 3/1/24.
//

#ifndef SORTING_CONFIG_H
#define SORTING_CONFIG_H

#include <cstddef>

constexpr size_t MAIN_MEMORY_SIZE = 400ULL * (1 << 30);

constexpr size_t THREAD_COUNT = 128;

// number of SSD directories in the file system
constexpr size_t SSD_COUNT= 30;
// number of SSDs that can be utilized in parallel; this is different from SSD_COUNT if RAID is used
constexpr size_t SSD_PARALLELISM = SSD_COUNT;

constexpr size_t SAMPLE_SORT_BUCKET_SIZE = 4 << 20;

constexpr size_t IO_VECTOR_SIZE = 1024;

constexpr size_t O_DIRECT_MULTIPLE = 4096;
// It should never be necessary to change this unless O_DIRECT_MULTIPLE is very large
constexpr size_t METADATA_SIZE = 2;
constexpr size_t IO_URING_BUFFER_SIZE = 60;

#endif //SORTING_CONFIG_H
