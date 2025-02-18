//
// Created by peter on 3/6/24.
//

#ifndef SORTING_RANDOM_NUMBER_GENERATOR_H
#define SORTING_RANDOM_NUMBER_GENERATOR_H

#include <cstdlib>
#include <string>

#include "parlay/sequence.h"

template<typename T>
parlay::sequence<T> RandomSequence(size_t n);

template <typename T>
void GenerateUniformRandomNumbers(const std::string &prefix, size_t count);

template <typename T>
void GenerateZipfianRandomNumbers(const std::string &prefix, size_t count, double s);

template<typename T>
void GenerateExponentialRandomNumbers(const std::string &prefix, size_t count, double s);

#endif //SORTING_RANDOM_NUMBER_GENERATOR_H
