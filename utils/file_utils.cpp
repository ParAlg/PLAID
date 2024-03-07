//
// Created by peter on 3/2/24.
//

#include "file_utils.h"
#include "config.h"
#include <filesystem>
using std::filesystem::directory_iterator;
using std::filesystem::path;

std::vector<FileInfo> FindFiles(const std::string &prefix, bool parallel) {
    // TODO: if this ever becomes the bottleneck, iterate through all SSDs in parallel
    std::vector<FileInfo> result;
    for (size_t i = 0; i < SSD_COUNT; i++) {
        path p("/mnt/ssd" + std::to_string(i));
        for (auto const& dir_entry : directory_iterator{p}) {
            auto path_str = dir_entry.path().string();
            if (path_str.find("/" + prefix) != std::string::npos) {
                result.emplace_back(path_str, dir_entry.file_size());
            }
        }
    }
    return result;
}
