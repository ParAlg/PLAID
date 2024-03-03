//
// Created by peter on 3/1/24.
//

#ifndef SORTING_CONFIG_H
#define SORTING_CONFIG_H

#include <cstddef>

const size_t MAIN_MEMORY_SIZE = 400ULL * (1 << 30);

// (advisory) size of a single file, may go a little higher in practice
const size_t SSD_COUNT= 30;
const size_t FILE_SIZE = 1 << 30;
const size_t DISK_BLOCK_SIZE = 4096;
const size_t IO_URING_BUFFER_SIZE = 60;

const size_t DEFAULT_SHARD_SIZE = 16 << 20;
size_t SHARD_SIZE = DEFAULT_SHARD_SIZE;



#endif //SORTING_CONFIG_H
