#ifndef SORTING_MAP_H
#define SORTING_MAP_H

#include <vector>

#include "parlay/primitives.h"
#include "absl/log/log.h"

#include "utils/file_info.h"
#include "utils/unordered_file_reader.h"
#include "utils/unordered_file_writer.h"

template <typename T, typename R = T, bool in_place = sizeof(T) == sizeof(R)>
void Map(std::vector<FileInfo> files, std::string result_prefix, std::function<R(T)> f) {
    UnorderedFileReader<T> reader;
    reader.PrepFiles(files);
    reader.Start(16, 8, 5);
    UnorderedFileWriter<R> writer(result_prefix, 8, 5, files.size());
    parlay::parallel_for(0, parlay::num_workers(), [&](size_t _) {
        while (true) {
            auto [ptr, n, file_index, element_index] = reader.Poll();
            if (ptr == nullptr) {
                break;
            }
            R *result;
            if (in_place) {
                // FIXME: strict aliasing violation here?
                for (size_t i = 0; i < n; i++) {
                    ptr[i] = f(ptr[i]);
                }
                result = (R *) ptr;
            } else {
                result = (R *) aligned_alloc(O_DIRECT_MULTIPLE, n * sizeof(R));
                for (size_t i = 0; i < n; i++) {
                    result[i] = f(ptr[i]);
                }
                reader.allocator.Free(ptr);
            }
            writer.Push(std::shared_ptr<R>(result, free), n, file_index, element_index * sizeof(R));
        }
    }, 1);
    writer.Wait();
}

#endif //SORTING_MAP_H
