#include <iostream>
#include <thread>
#include "utils/unordered_file_writer.h"

int main() {
    auto writer = std::make_unique<UnorderedFileWriter<int>>("numbers");
    std::cout << "[1]    114514 Segmentation fault (core dumped)" << std::endl;
    return 0;
}
