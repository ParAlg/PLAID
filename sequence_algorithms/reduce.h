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
    UnorderedFileReader<T> reader;
    reader.PrepFiles(files);
    reader.Start();
    return parlay::reduce(parlay::map(parlay::iota(parlay::num_workers()), [&](size_t worker_index) {
        R result = monoid.identity;
        while (true) {
            auto [ptr, n, _, _2] = reader.Poll();
            if (n == 0) {
                break;
            }
            auto slice = parlay::make_slice(ptr, ptr + n);
            result = monoid(result, parlay::reduce(slice, monoid));
            free(ptr);
        }
        return result;
    }, 1), monoid);
}

#endif //SORTING_REDUCE_H
