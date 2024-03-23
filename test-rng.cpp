//
// Created by peter on 3/6/24.
//

#include <iostream>
#include "utils/logger.h"
#include "utils/random_number_generator.h"
#include "utils/unordered_file_reader.h"
#include <set>

int main() {
    const std::string PREFIX = "numbers";
    InitLogger();
    auto writer_result = GenerateUniformRandomNumbers<int>(PREFIX, 1 << 20);
    std::set<int> writer_values;
    for (auto [ptr, size] :writer_result) {
        for (size_t i = 0; i < size; i++) {
            writer_values.insert(ptr.get()[i]);
        }
    }
    UnorderedFileReader<int> reader;
    reader.PrepFiles(PREFIX);
    std::set<int> reader_values;
    while (true) {
        auto [ptr, size] = reader.Poll();
        if (ptr == nullptr) {
            break;
        }
        for (size_t i = 0; i < size; i++) {
            reader_values.insert(ptr[i]);
        }
        delete[] ptr;
    }
    if (reader_values.size() != writer_values.size()) {
        std::cout << reader_values.size() << " " << writer_values.size() << std::endl;
    }
    if (reader_values != writer_values) {
        std::cout << "Not equal!" << std::endl;
    }
    std::cout << "[1]    114514 Segmentation fault (core dumped)" << std::endl;
    return 0;
}
