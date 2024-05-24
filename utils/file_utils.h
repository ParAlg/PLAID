//
// Created by peter on 3/2/24.
//

#ifndef SORTING_FILE_UTILS_H
#define SORTING_FILE_UTILS_H

#include <vector>
#include <string>
#include "utils/file_info.h"
#include "configs.h"

std::vector<FileInfo> FindFiles(const std::string &prefix, bool parallel = false);

void GetFileInfo(std::vector<FileInfo> &info);

/**
 * Ensure that a byte offset conforms to disk alignment requirements by rounding down.
 * For example, 4098 would become 4096 if O_DIRECT_MULTIPLE is set to 4096.
 *
 * @param original
 * @return
 */
constexpr size_t AlignDown(size_t original) {
    return original / O_DIRECT_MULTIPLE * O_DIRECT_MULTIPLE;
}
/**
 * Ensure that a byte offset conforms to disk alignment requirements by rounding up.
 * For example, 4098 would become 8192 if O_DIRECT_MULTIPLE is set to 4096.
 *
 * @param original
 * @return
 */
constexpr size_t AlignUp(size_t original) {
    return (original + O_DIRECT_MULTIPLE - 1) / O_DIRECT_MULTIPLE * O_DIRECT_MULTIPLE;
}

void ReadFileOnce(const std::string &file_name, void* buffer, size_t offset);
void ReadFileOnce(const std::string &file_name, void *buffer, size_t start, size_t read_size);

void* ReadEntireFile(const std::string &file_name, size_t read_size);

std::string GetFileName(const std::string &prefix, size_t file_number);

#endif //SORTING_FILE_UTILS_H
