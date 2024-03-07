//
// Created by peter on 3/6/24.
//

#include <iostream>
#include "utils/logger.h"
#include "utils/random_number_generator.h"

int main() {
    InitLogger();
    GenerateUniformRandomNumbers("numbers", 1 << 25);

    std::cout << "[1]    114514 Segmentation fault (core dumped)" << std::endl;
    return 0;
}
