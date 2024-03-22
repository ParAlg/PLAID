//
// Created by peter on 3/21/24.
//

#ifndef SORTING_FILE_INFO_H
#define SORTING_FILE_INFO_H

#include <string>
#include <utility>

struct FileInfo {
    std::string file_name;
    size_t true_size = 0;
    size_t file_size = 0;

    FileInfo() = default;

    FileInfo(std::string file_name, size_t true_size, size_t file_size) :
            file_name(std::move(file_name)), true_size(true_size), file_size(file_size) {}
};


#endif //SORTING_FILE_INFO_H
