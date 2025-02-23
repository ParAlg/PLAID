//
// Created by peter on 6/4/24.
//

#ifndef SORTING_REDUCE_H
#define SORTING_REDUCE_H

#include <vector>
#include <queue>

#include "parlay/primitives.h"
#include "absl/log/log.h"
#include "absl/log/check.h"

#include "utils/file_info.h"
#include "utils/unordered_file_reader.h"

template <typename T, typename R = T, typename Monoid>
R Reduce(std::vector<FileInfo> files, Monoid monoid) {
    using Reader = UnorderedFileReader<T, READER_READ_SIZE>;
    Reader reader;
    reader.PrepFiles(files);
    reader.Start(64, 32, 4);
    return parlay::reduce(parlay::map(parlay::iota(parlay::num_workers()), [&](size_t worker_index) {
        R result = monoid.identity;
        while (true) {
            auto [ptr, n, _, _2] = reader.Poll();
            if (n == 0) {
                break;
            }
            for (size_t i = 0; i < n; i++) {
                result = monoid(result, ptr[i]);
            }
            Reader::ReaderAllocator::free(reinterpret_cast<typename Reader::ReaderData*>(ptr));
        }
        return result;
    }, 1), monoid);
}

#endif //SORTING_REDUCE_H
