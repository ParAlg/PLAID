//
// Created by peter on 3/2/24.
//

#ifndef SORTING_FILE_UTILS_H
#define SORTING_FILE_UTILS_H

#include <vector>
#include <string>

typedef std::tuple<std::string, size_t> FileInfo;
std::vector<FileInfo> FindFiles(const std::string &prefix, int file_count);

#endif //SORTING_FILE_UTILS_H
