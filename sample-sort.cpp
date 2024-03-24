//
// Created by peter on 3/21/24.
//

#include <random>

#include "sorter/sample_sort.h"
#include "utils/random_number_generator.h"

template <typename T, typename Iterator>
void CompareSortingResult(Iterator start, Iterator end, const std::vector<FileInfo> &file_list) {
    auto cur = start;
    size_t cur_index = 0;
    size_t mismatch_count = 0;
    for (const auto &f : file_list) {
        auto current_file = (T*)ReadEntireFile(f.file_name, f.true_size);
        size_t n = f.true_size / sizeof(T);
        for (size_t i = 0; i < n; i++) {
            if (cur == end) {
                LOG(ERROR) << "Iterator for in-memory result has unexpectedly ended at index " << cur_index;
                goto too_many_errors;
            }
            if (*cur != current_file[i]) {
                LOG(ERROR) << "Mismatch at index " << cur_index << ". Expected " << *cur << ". Got " << current_file[i];
                mismatch_count++;
                if (mismatch_count >= 100) {
                    goto too_many_errors;
                }
            }
            cur++;
            cur_index++;
        }
        free(current_file);
    }
    if (mismatch_count == 0) {
        LOG(INFO) << "No mismatch found";
    }
    return;
    too_many_errors:
    LOG(ERROR) << "Too many errors. Exiting...";
    exit(0);
}

void TestSampleSort() {
    using Type = uint64_t;
    std::string input_prefix = "numbers";
    std::string output_prefix = "sorted_numbers";
    LOG(INFO) << "Generating random numbers and writing them to disk";
    auto nums = GenerateUniformRandomNumbers<Type>(input_prefix, 1 << 15);
    // in memory sorting
    LOG(INFO) << "Performing in-memory sorting";
    auto all_nums = parlay::flatten(parlay::map(parlay::iota(nums.size()), [&](size_t i) {
        return parlay::sequence<Type>(nums[i].first.get(), nums[i].first.get() + nums[i].second);
    }));
    parlay::sort_inplace(all_nums, std::less<>());
    // external memory sorting
    LOG(INFO) << "Performing external memory sorting";
    SampleSort<Type, std::less<>> sorter;
    auto input_files = FindFiles(input_prefix);
    auto result_files = sorter.Sort(input_files, output_prefix, std::less<>());

    LOG(INFO) << "Comparing result";
    CompareSortingResult<size_t>(all_nums.begin(), all_nums.end(), result_files);
}

std::shared_ptr<size_t> GenerateSmallSample(const std::string &prefix, size_t n) {
    UnorderedFileWriter<size_t> writer(prefix);
    auto nums = std::shared_ptr<size_t>((size_t*)malloc(n * sizeof(size_t)), free);
    for (size_t i = 0; i < n; i++) {
        nums.get()[i] = i + 1;
    }
    // shuffle these values, write them to disk, and once we're done writing, we can sort them again
    std::shuffle(nums.get(), nums.get() + n, std::mt19937(std::random_device()()));
    writer.Push(nums, n);
    writer.Close();
    writer.Wait();
    std::sort(nums.get(), nums.get() + n);
    return nums;
}

void TestSampleSortSmall() {
    const std::string prefix = "numbers", result_prefix = "result";
    size_t n = 4096;
    auto nums = GenerateSmallSample(prefix, n);
    SampleSort<size_t, std::less<>> sorter;
    auto file_list = FindFiles(prefix);
    auto result_files = sorter.Sort(file_list, result_prefix, std::less<>());
    CompareSortingResult<size_t>(nums.get(), nums.get() + n, result_files);
}

int main() {
    TestSampleSortSmall();
}
