
//
// Created by peter on 3/21/24.
//

#include <random>

#include "scatter_gather_algorithms/permutation.h"
#include "utils/command_line.h"

long parse_long(char *string) {
    return std::strtol(string, nullptr, 10);
}

void generate(int argc, char **argv) {
    if (argc < 5) {
        LOG(ERROR) << "Usage: " << argv[0] << " gen <data size (power of 2)> <prefix> <element size (in bytes)>";
        return;
    }
    size_t n = 1UL << parse_long(argv[2]);
    std::string prefix(argv[3]);
    unsigned int element_size = (bool)parse_long(argv[4]);
    LOG(INFO) << "Generating " << n << " elements of size " << element_size << " with file prefix " << prefix;
}

void run_test(int argc, char **argv) {
    if (argc < 4) {
        LOG(ERROR) << "Usage: " << argv[0] << " run <input prefix> <output prefix>";
        return;
    }
    std::string input_prefix(argv[2]), output_prefix(argv[3]);
    auto input_files = FindFiles(input_prefix);
    Permutation<size_t> permutation;
    parlay::internal::timer timer("Sample sort");
    auto result_files = permutation.Permute(input_files, output_prefix);
    timer.next("DONE");
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
            {"gen", generate},
            {"run", run_test},
        }
    );
    if (commands.count(argv[1])) {
        commands[argv[1]](argc, argv);
    } else {
        goto show_usage;
    }
}

