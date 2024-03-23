//
// Created by peter on 3/2/24.
//

#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>

#include "parlay/parallel.h"

#include "file_utils.h"
#include "config.h"
#include "utils/logger.h"

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
                result.emplace_back(path_str, 0, dir_entry.file_size());
            }
        }
    }
    return result;
}

void GetFileInfo(std::vector<FileInfo> &info) {
    parlay::parallel_for(0, info.size(), [&](size_t i){
        if (info[i].file_size == 0) {
            struct stat stat_buf;
            SYSCALL(stat(info[i].file_name.c_str(), &stat_buf));
            info[i].file_size = stat_buf.st_size;
        }
        if (info[i].true_size == 0) {
            uint8_t buffer[O_DIRECT_MULTIPLE];
            ReadFileOnce(info[i].file_name, buffer, info[i].file_size - O_DIRECT_MULTIPLE);
            info[i].true_size = info[i].file_size - *(uint16_t*)(buffer + O_DIRECT_MULTIPLE - METADATA_SIZE);
        }
    });
}

std::string GetFileName(const std::string &prefix, size_t file_number) {
    size_t ssd_number = file_number % SSD_COUNT;
    return "/mnt/ssd" + std::to_string(ssd_number) + "/" + prefix + std::to_string(file_number);
}

void ReadFileOnce(const std::string &file_name, void* buffer, size_t offset) {
    int fd = open(file_name.c_str(), O_RDONLY | O_DIRECT);
    SYSCALL(fd);
    auto res = lseek64(fd, (long)offset, SEEK_SET);
    SYSCALL(res);
    SYSCALL(read(fd, buffer, O_DIRECT_MULTIPLE));
    close(fd);
}

void *ReadEntireFile(const std::string &file_name, size_t read_size) {
    if (read_size % O_DIRECT_MULTIPLE != 0) {
        read_size += O_DIRECT_MULTIPLE - read_size % O_DIRECT_MULTIPLE;
    }
    void *buffer = malloc(read_size);
    int fd = open(file_name.c_str(), O_RDONLY | O_DIRECT);
    SYSCALL(fd);
    SYSCALL(read(fd, buffer, read_size));
    return buffer;
}
