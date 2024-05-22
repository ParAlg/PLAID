//
// Created by peter on 3/21/24.
//

#include <random>

#include "sorter/sample_sort.h"
#include "utils/random_number_generator.h"

template<typename T, typename Iterator>
parlay::sequence<std::tuple<size_t, T>> CompareSortingResultFile(Iterator start, size_t start_index, const FileInfo &f) {
    start += start_index;
    auto current_file = (T *) ReadEntireFile(f.file_name, f.true_size);
    size_t n = f.true_size / sizeof(T);
    parlay::sequence<std::tuple<size_t, T>> mismatch_indices;
    for (size_t i = 0; i < n; i++) {
        auto expected = *start;
        auto actual = current_file[i];
        if (expected != actual) {
            mismatch_indices.push_back({start_index, actual});
            if (mismatch_indices.size() > 10) {
                return mismatch_indices;
            }
        }
        start++;
        start_index++;
    }
    free(current_file);
    return mismatch_indices;
}

/**
 * Compare the result of an in-memory sorting algorithm to an external memory sorting algorithm.
 *
 * @tparam T The type of data being sorted.
 * @tparam Iterator An iterator for the result of the in-memory sorting algorithm.
 * @param start Starting iterator
 * @param end Ending iterator
 * @param file_list A list of files resulting from the sorting algorithm
 */
template<typename T, typename Iterator>
void CompareSortingResult(Iterator start, Iterator end, const std::vector<FileInfo> &file_list) {
    // read each file and then compare the content of the file to the result of the in-memory sorting algorithm
    size_t total_size = std::distance(start, end);
    size_t n_files = file_list.size();
    size_t prefix_sum[n_files + 1];
    prefix_sum[0] = 0;
    for (size_t i = 1; i < n_files; i++) {
        prefix_sum[i] = prefix_sum[i - 1] + file_list[i - 1].true_size / sizeof(T);
    }
    size_t file_size = prefix_sum[n_files - 1] + file_list[n_files - 1].true_size / sizeof(T);
    if (file_size != total_size) {
        LOG(ERROR) << "Expected " << total_size << " numbers, got " << file_size;
        return;
    }
    auto mismatches = parlay::flatten(parlay::map(parlay::iota(n_files), [&](size_t i){
        return CompareSortingResultFile<T>(start, prefix_sum[i], file_list[i]);
    }, 1));
    if (mismatches.empty()) {
        LOG(INFO) << "No mismatch found after comparing " << total_size << " elements.";
        return;
    }
    for (size_t i = 0; i < std::min(10UL, mismatches.size()); i++) {
        auto [index, value] = mismatches[i];
        LOG(ERROR) << "Mismatch at index " << i << ": expected " << start[index] << ", got " << value << " instead.";
    }
}

/**
 * This is a more cluncky testing function. Don't read this.
 */
void TestSampleSort() {
    using Type = uint64_t;
    std::string input_prefix = "numbers";
    std::string output_prefix = "sorted_numbers";
    LOG(INFO) << "Generating random numbers and writing them to disk";
    auto nums = GenerateUniformRandomNumbers<Type>(input_prefix, 1 << 20);
    // in memory sorting
    LOG(INFO) << "Performing in-memory sorting";
    auto all_nums = parlay::flatten(parlay::map(parlay::iota(nums.size()), [&](size_t i) {
        return parlay::sequence<Type>(nums[i].first.get(), nums[i].first.get() + nums[i].second);
    }));
    parlay::sort_inplace(all_nums, std::less<>());
    // external memory sorting
    LOG(INFO) << "Performing external memory sorting";
    SampleSort<Type> sorter;
    auto input_files = FindFiles(input_prefix);
    auto result_files = sorter.Sort(input_files, output_prefix, std::less<>());

    LOG(INFO) << "Comparing result";
    CompareSortingResult<size_t>(all_nums.begin(), all_nums.end(), result_files);
}

void nop(void* ptr) {}

/**
 * Write some small numbers to disk for testing purposes.
 *
 * @param prefix The prefix of the names of the resulting files
 * @param n Number of items to be generated
 * @return A pointer to the resulting numbers
 */
std::shared_ptr<size_t> GenerateSmallSample(const std::string &prefix, size_t n) {
    auto nums = (size_t *) malloc(n * sizeof(size_t));
    {
        auto perm = parlay::random_permutation(n, parlay::random(std::random_device()()));
        parlay::parallel_for(0, n, [&](size_t i) {
            nums[i] = perm[i];
        });
    }
    UnorderedFileWriter<size_t> writer(prefix, 128, 2);
    size_t step = std::min(1UL << 20, n);
    for (size_t i = 0; i < n; i += step) {
        writer.Push(std::shared_ptr<size_t>(nums + i, nop), std::min(step, n - i));
    }
    writer.Close();
    writer.Wait();
    parlay::parallel_for(0, n, [&](size_t i) {
        nums[i] = i;
    });
    return {nums, free};
}

std::shared_ptr<size_t> GetDummyArray(size_t n) {
    std::shared_ptr<size_t> result((size_t *) malloc(n * sizeof(size_t)), free);
    auto *arr = result.get();
    parlay::parallel_for(0, n, [&](size_t i) {
        arr[i] = i;
    });
    return result;
}

/**
 * Test the sorter with a small data sample
 * @param n Number of items in the test
 */
void TestSampleSortSmall(size_t n, bool generate) {
    const std::string prefix = "numbers", result_prefix = "result";
    std::shared_ptr<size_t> nums;
    if (generate) {
        nums = GenerateSmallSample(prefix, n);
    } else {
        nums = GetDummyArray(n);
    }
    SampleSort<size_t> sorter;
    // Note that we don't know the number of files in the list. We just know the prefix.
    // We need to look for the files generated by GenerateSmallSample.
    auto file_list = FindFiles(prefix);
    LOG(INFO) << "Performing external memory sort.";
    auto result_files = sorter.Sort(file_list, result_prefix, std::less<>());
    LOG(INFO) << "Comparing sorting result.";
    CompareSortingResult<size_t>(nums.get(), nums.get() + n, result_files);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        show_usage:
        LOG(ERROR) << "Usage: " << argv[0] << " <data size> <true|false>";
        return 0;
    }
    size_t n = 1UL << std::strtol(argv[1], nullptr, 10);
    bool regenerate;
    if (strcmp(argv[2], "true") == 0) {
        regenerate = true;
    } else if (strcmp(argv[2], "false") == 0) {
        regenerate = false;
    } else {
        goto show_usage;
    }
    LOG(INFO) << "Testing with size " << n << " and generate: " << regenerate;
    TestSampleSortSmall(n, regenerate);
}
