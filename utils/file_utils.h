//
// Created by peter on 3/2/24.
//

#ifndef SORTING_FILE_UTILS_H
#define SORTING_FILE_UTILS_H

#include <vector>
#include <string>
#include "utils/file_info.h"

std::vector<FileInfo> FindFiles(const std::string &prefix, bool parallel = false);

void GetFileInfo(std::vector<FileInfo> &info);

void ReadFileOnce(const std::string &file_name, void* buffer, size_t offset);
void ReadFileOnce(const std::string &file_name, void *buffer, size_t start, size_t read_size);

void* ReadEntireFile(const std::string &file_name, size_t read_size);

std::string GetFileName(const std::string &prefix, size_t file_number);

#endif //SORTING_FILE_UTILS_H
