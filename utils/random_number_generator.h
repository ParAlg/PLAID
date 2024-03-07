//
// Created by peter on 3/6/24.
//

#ifndef SORTING_RANDOM_NUMBER_GENERATOR_H
#define SORTING_RANDOM_NUMBER_GENERATOR_H

#include "utils/unordered_file_writer.h"

void WriteUniformRandomNumbers(UnorderedFileWriter<int> *writer, size_t count, size_t granularity);

void GenerateUniformRandomNumbers(const std::string &prefix, size_t count, size_t nums_per_thread = 0);

#endif //SORTING_RANDOM_NUMBER_GENERATOR_H
