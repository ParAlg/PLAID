//
// Created by peter on 3/2/24.
//

#ifndef SORTING_FILE_UTILS_H
#define SORTING_FILE_UTILS_H

#include <vector>
#include <string>
#include "utils/file_info.h"

std::vector<FileInfo> FindFiles(const std::string &prefix, bool parallel = false);

std::string GetFileName(const std::string &prefix, size_t file_number);

#endif //SORTING_FILE_UTILS_H
