//
// Created by peter on 3/21/24.
//

#include "sorter/sample_sort.h"
#include "utils/random_number_generator.h"

void TestSampleSort() {
    using Type = uint64_t;
    std::string input_prefix = "numbers";
    std::string output_prefix = "sorted_numbers";
    // in memory sorting
    auto nums = GenerateUniformRandomNumbers<Type>(input_prefix, 1 << 24);
    auto all_nums = parlay::flatten(parlay::map(parlay::iota(nums.size()), [&](size_t i) {
        return parlay::sequence<Type>(nums[i].first.get(), nums[i].first.get() + nums[i].second);
    }));
    parlay::sort_inplace(all_nums, std::greater<>());
    // external memory sorting
    SampleSort<Type, std::greater<>> sorter;
    auto files = FindFiles(input_prefix);
    sorter.Sort(files, output_prefix, std::greater<>());
}

int main() {
    TestSampleSort();
}
