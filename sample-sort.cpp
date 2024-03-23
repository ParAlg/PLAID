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
    auto input_files = FindFiles(input_prefix);
    auto result_files = sorter.Sort(input_files, output_prefix, std::greater<>());
    size_t compare_index = 0;
    for (const auto &f : result_files) {
        auto current_file = (Type*)ReadEntireFile(f.file_name, f.true_size);
        size_t n = f.true_size / sizeof(Type);
        for (size_t i = 0; i < n; i++) {
            if (all_nums[compare_index + i] != current_file[i]) {
                LOG(ERROR) << "Mismatch at index " << compare_index + i << ". Expected " << all_nums[compare_index + i] << ". Got " << current_file[i];
            }
        }
        compare_index += n;
    }
}

int main() {
    TestSampleSort();
}
