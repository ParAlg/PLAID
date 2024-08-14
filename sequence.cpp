//
// Created by peter on 8/14/24.
//

#include "utils/command_line.h"
#include <map>
#include <functional>
#include <string>

#include "absl/log/log.h"
#include "sequence_algorithms/reduce.h"

parlay::monoid monoid([](size_t a, size_t b) {
    return a ^ b;
}, 0);

void RunReduce(int argc, char **argv) {
    CHECK(argc >= 3);
    std::string prefix(argv[2]);
    auto files = FindFiles(prefix);
    auto result = Reduce<size_t>(files, monoid);
    LOG(INFO) << "Result: " << result;
}

void VerifyReduce(int argc, char **argv) {
    CHECK(argc >= 4);
    using T = size_t;
    std::string prefix(argv[2]);
    std::string previous_result(argv[3]);
    auto files = FindFiles(prefix);
    GetFileInfo(files);
    T result = parlay::reduce(parlay::map(files, [](const FileInfo &file) {
        auto ptr = (T *) ReadEntireFile(file.file_name, file.file_size);
        auto slice = parlay::make_slice(ptr, ptr + file.file_size / sizeof(T));
        return parlay::reduce(slice, monoid);
    }), monoid);
    if (std::to_string(result) == previous_result) {
        LOG(INFO) << "Test passed";
    } else {
        LOG(ERROR) << "Expected " << result << ", got " << previous_result;
    }
}

int main(int argc, char **argv) {
    ParseGlobalArguments(argc, argv);
    if (argc < 2) {
        show_usage:
        LOG(ERROR) << "Usage: " << argv[0] << " <gen|run> <command-specific options>";
        return 0;
    }
    std::map<std::string, std::function<void(int, char **)>> commands(
        {
            {"reduce",        RunReduce},
            {"verify_reduce", VerifyReduce}
        }
    );
    if (commands.count(argv[1])) {
        commands[argv[1]](argc, argv);
    } else {
        goto show_usage;
    }
}
