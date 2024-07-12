//
// Created by peter on 3/2/24.
//

#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>

#include "parlay/parallel.h"

#include "file_utils.h"
#include "./configs.h"
#include "utils/logger.h"

using std::filesystem::directory_iterator;
using std::filesystem::path;

/**
 * Find files whose name begin with the specified prefix.
 *
 * @param prefix All files that begin with prefix will be included in the result
 * @param parallel Whether to do this in parallel. Currently not used.
 * @return A list of file information.
 *   Note that we also get the file size as a by-product of iterating through the directory.
 */
std::vector<FileInfo> FindFiles(const std::string &prefix, bool parallel) {
    // TODO: if this ever becomes the bottleneck, iterate through all SSDs in parallel
    std::vector<FileInfo> result;
    // go through every SSD
    for (size_t i = 0; i < SSD_COUNT; i++) {
        path p("/mnt/ssd" + std::to_string(i));
        for (auto const &dir_entry: directory_iterator{p}) {
            auto path_str = dir_entry.path().string();
            size_t index = path_str.find("/" + prefix);
            if (index != std::string::npos) {
                // true_size is 0 since we don't know it
                size_t file_index = std::stol(path_str.substr(index + 1 + prefix.size()));
                result.emplace_back(path_str, file_index, 0, dir_entry.file_size());
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const FileInfo& a, const FileInfo& b) {
        return a.file_index < b.file_index;
    });
    return result;
}

/**
 * @brief Retrieve information about the file if not available.
 *   Specifically, the size of the file on the file system and the true size of the file
 *   (excluding the garbage data at the end, which is used to pad spaces
 *   so that the file size is a multiple of O_DIRECT_MULTIPLE)
 *
 * @param info A list of files
 */
void GetFileInfo(std::vector<FileInfo> &info) {
    parlay::parallel_for(0, info.size(), [&](size_t i) {
        if (info[i].file_size == 0) {
            struct stat stat_buf;
            SYSCALL(stat(info[i].file_name.c_str(), &stat_buf));
            info[i].file_size = stat_buf.st_size;
        }
        // FIXME: all files should have end-of-file marker?
        // info[i].true_size = info[i].file_size;
        if (info[i].true_size == 0 && info[i].file_size > 0) {
            unsigned char buffer[O_DIRECT_MULTIPLE];
            ReadFileOnce(info[i].file_name, buffer, info[i].file_size - O_DIRECT_MULTIPLE);
            info[i].true_size = info[i].file_size - *(uint16_t *) (buffer + O_DIRECT_MULTIPLE - METADATA_SIZE);
        }
    }, 1);
}

/**
 * Generate a file name and, in doing so, assign it to a SSD in a round-robin fashion
 *
 * @param prefix
 * @param file_number
 * @return
 */
std::string GetFileName(const std::string &prefix, size_t file_number) {
    size_t ssd_number = file_number % SSD_COUNT;
    return "/mnt/ssd" + std::to_string(ssd_number) + "/" + prefix + std::to_string(file_number);
}

/**
 * Read O_DIRECT_MULTIPLE bytes starting from the specified offset of the file
 *
 * @param file_name
 * @param buffer Buffer to which the result will be written. Its size must be at least O_DIRECT_MULTIPLE
 * @param offset The byte at which we want to start reading
 */
void ReadFileOnce(const std::string &file_name, void *buffer, size_t offset) {
    int fd = open(file_name.c_str(), O_RDONLY | O_DIRECT);
    SYSCALL(fd);
    CHECK(offset % O_DIRECT_MULTIPLE == 0)
                << "File read offset is " << offset << ", which is not a multiple of " << O_DIRECT_MULTIPLE;
    auto res = lseek64(fd, (long) offset, SEEK_SET);
    CHECK(res != off64_t(-1)) << std::strerror(errno) << " " << file_name << " at offset " << offset;
    SYSCALL(read(fd, buffer, O_DIRECT_MULTIPLE));
    SYSCALL(close(fd));
}

/**
 * Read read_size bytes from a file and store the result in a buffer. The read starts at <code>start</code> bytes.
 * This version is slightly slower since it copies memory twice. However, it is easier to use since no alignment
 * assumptions are made about start and read_size.
 *
 * @param file_name
 * @param buffer
 * @param start
 * @param read_size Must be much smaller than stack size limit (i.e. 8MB by default)
 * since the read buffer for this function is allocated on the stack.
 */
void ReadFileOnce(const std::string &file_name, void *buffer, size_t start, size_t read_size) {
    int fd = open(file_name.c_str(), O_RDONLY | O_DIRECT);
    SYSCALL(fd);
    size_t start_aligned = AlignDown(start);
    size_t end = start + read_size;
    // compute the nearest aligned byte
    size_t end_aligned = AlignUp(end);
    size_t aligned_read_size = end_aligned - start_aligned;
    auto res = lseek64(fd, (long) start_aligned, SEEK_SET);
    SYSCALL(res);
    // read everything into a temporary buffer and then copy the data to the original buffer
    unsigned char temp_buffer[aligned_read_size];
    SYSCALL(read(fd, temp_buffer, O_DIRECT_MULTIPLE));
    memcpy(buffer, temp_buffer + (start - start_aligned), read_size);
    SYSCALL(close(fd));
}

/**
 * Read an entire file into memory and store the result in a buffer
 *
 * @param file_name
 * @param read_size Number of bytes to be read
 * @return A pointer to the content of the file (needs to be freed by the caller using free)
 */
void *ReadEntireFile(const std::string &file_name, size_t read_size) {
    // align the read for O_DIRECT
    read_size = AlignUp(read_size);
    void *buffer = malloc(read_size);
    int fd = open(file_name.c_str(), O_RDONLY | O_DIRECT);
    SYSCALL(fd);
    auto result_size = read(fd, buffer, read_size);
    SYSCALL(result_size);
    ASSERT((size_t) result_size == read_size,
           "Size mismatch for " << file_name << ": " << read_size << " attempted, got " << result_size);
    return buffer;
}
