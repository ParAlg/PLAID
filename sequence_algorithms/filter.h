//
// Created by peter on 8/29/24.
//

#ifndef SORTING_FILTER_H
#define SORTING_FILTER_H

#include "utils/file_info.h"
#include "utils/unordered_file_reader.h"
#include "utils/unordered_file_writer.h"

template<typename T>
void FilterFile(const FileInfo &in_file, FileInfo &out_file, std::function<bool(const T)> predicate) {
    struct QueueData {
        T *ptr;
        size_t size;
        size_t index;

        QueueData(T *ptr, size_t size, size_t index) : ptr(ptr), size(size), index(index) {}
    };
    const auto cmp = [](const QueueData a, const QueueData b) {
        return std::get<2>(a) < std::get<2>(b);
    };
    UnorderedFileReader<T> reader;
    reader.PrepFiles({in_file});
    reader.Start(1 << 20, 4, 4, 1);
    UnorderedFileWriter<T> writer({out_file.file_name}, 4, 1);
    std::priority_queue<QueueData, decltype(cmp)> queue;
    size_t next_index = in_file.before_size;
    constexpr size_t buffer_size_bytes = 4 << 20, buffer_size = buffer_size_bytes / sizeof(T);
    size_t buffer_index = 0;
    std::shared_ptr<T> buffer((T*)malloc(buffer_size_bytes), free);
    while (true) {
        auto [ptr, size, _, index] = reader.Poll();
        if (ptr == nullptr) {
            CHECK(queue.empty());
            break;
        }
        queue.emplace(ptr, size, index);
        while (!queue.empty()) {
            auto top = queue.top();
            if (top.index != next_index) {
                break;
            }
            queue.pop();
            next_index += size;
            // process buffer
            size_t i = 0;
            while (i < size) {
                if (predicate(ptr[i])) {
                    buffer[buffer_index] = ptr[i];
                    buffer_index++;
                }
                if (buffer_size == buffer_index) {
                    writer.Push(std::move(buffer), buffer_size);
                    buffer = std::shared_ptr<T>(malloc(buffer_size_bytes), free);
                }
                i++;
            }
        }
        if (buffer_index != 0) {
            MakeFileEndMarker((unsigned char*)buffer.get(),
                              buffer_size_bytes,
                              buffer_index * sizeof(T));
            writer.Push(std::move(buffer), buffer_size);
        }
    }
}

#endif //SORTING_FILTER_H
