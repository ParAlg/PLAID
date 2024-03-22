//
// Created by peter on 3/2/24.
//

#ifndef SORTING_LOGGER_H
#define SORTING_LOGGER_H

#include <cerrno>
#include <cstring>
#include "absl/log/log.h"

#define SYSCALL(expr) do { \
    int result = (expr);   \
    if (__builtin_expect(result < 0, 0)) LOG(ERROR) << "System call returned " << result << ": " \
    << std::strerror(errno); \
} while(0)

void InitLogger();

#endif //SORTING_LOGGER_H
