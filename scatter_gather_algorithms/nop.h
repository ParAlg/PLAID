#include "scatter_gather_algorithms/scatter_gather.h"

template <typename T>
void ScatterGatherNop(const std::string &input_prefix, size_t num_buckets) {
    ScatterGather<T> scatter_gather;
    auto files = FindFiles(input_prefix);
    scatter_gather.Run(files, "results", num_buckets,
                       [=](size_t x, size_t index) {return (x + index) % num_buckets;},
                       [](size_t** ptr, size_t length) {return;},
                       5);
}
