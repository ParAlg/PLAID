//
// Created by peter on 8/29/24.
//

#ifndef SORTING_FILTER_H
#define SORTING_FILTER_H

#include "utils/file_info.h"
#include "utils/unordered_file_reader.h"

template<typename T>
void FilterFile(const FileInfo &in_file, FileInfo &out_file) {
    struct QueueData {
        T *ptr;
        size_t size;
        size_t index;

        QueueData(T *ptr, size_t size, size_t index) : ptr(ptr), size(size), index(index) {}
    };
    auto cmp = [](const QueueData a, const QueueData b) {
        return std::get<2>(a) < std::get<2>(b);
    };
    UnorderedFileReader<T> reader;
    reader.PrepFiles({in_file});
    reader.Start(1 << 20, 4, 4, 1);
    std::priority_queue<QueueData, decltype(cmp)> queue;
    size_t next_index = in_file.before_size;
    while (true) {
        auto [ptr, size, _, index] = reader.Poll();
        if (ptr == nullptr) {
            CHECK(queue.empty());
            break;
        }
        if (index == next_index) {
            // do something
            continue;
        }
        queue.emplace(ptr, size, index);
    }
}

#endif //SORTING_FILTER_H
