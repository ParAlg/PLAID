#ifndef SORTING_MAP_H
#define SORTING_MAP_H

#include <vector>

#include "parlay/primitives.h"
#include "absl/log/log.h"

#include "utils/file_info.h"
#include "utils/unordered_file_reader.h"
#include "utils/unordered_file_writer.h"

template <typename T, typename R = T>
void Map(std::vector<FileInfo> files, std::string result_prefix, std::function<R(T)> f) {
    UnorderedFileReader<T> reader;
    reader.PrepFiles(files);
    reader.Start(1 << 20, 64, 64, 2);
    UnorderedFileWriter<R> writer(result_prefix, 64, 2, files.size());
    parlay::parallel_for(0, parlay::num_workers(), [&](size_t _) {
        while (true) {
            auto [ptr, n, file_index, element_index] = reader.Poll();
            if (ptr == nullptr) {
                break;
            }
            R *result;
            if (sizeof(T) == sizeof(R)) {
                for (size_t i = 0; i < n; i++) {
                    ptr[i] = f(ptr[i]);
                }
                result = (R *) ptr;
            } else {
                result = (R *) malloc(n * sizeof(R));
                for (size_t i = 0; i < n; i++) {
                    result[i] = f(ptr[i]);
                }
                free(ptr);
            }
            writer.Push(std::shared_ptr<R>(result, free), n, file_index, element_index * sizeof(R));
        }
    }, 1);
    writer.Wait();
}

#endif //SORTING_MAP_H
