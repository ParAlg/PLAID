//
// Created by peter on 3/6/24.
//

#ifndef SORTING_RANDOM_NUMBER_GENERATOR_H
#define SORTING_RANDOM_NUMBER_GENERATOR_H

#include "utils/unordered_file_writer.h"

template <typename T>
using PointerVector = std::vector<std::pair<std::shared_ptr<T[]>, size_t>>;

template <typename T>
PointerVector<T> GenerateUniformRandomNumbers(const std::string &prefix, size_t count, size_t nums_per_thread = 0);

#endif //SORTING_RANDOM_NUMBER_GENERATOR_H
