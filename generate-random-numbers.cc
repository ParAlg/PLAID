#include <iostream>
#include "utils/logger.h"
#include "utils/random_number_generator.h"

int main() {
    InitLogger();
    // GenerateUniformRandomNumbers("numbers", 1 << 20);
    GenerateUniformRandomNumbers<int>("numbers", 128, 128);
    std::cout << "[1]    114514 Segmentation fault (core dumped)" << std::endl;
    return 0;
}
