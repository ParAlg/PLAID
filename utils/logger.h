//
// Created by peter on 3/2/24.
//

#ifndef SORTING_LOGGER_H
#define SORTING_LOGGER_H

#include <cerrno>
#include <cstring>
#include "absl/log/log.h"

/**
 * Macro for error checking after doing a system call. If an error is produced, print the resulting (negative)
 * number and print errno.
 */
#define SYSCALL(expr) do { \
    int result = (expr);   \
    if (__builtin_expect(result < 0, 0)) LOG(ERROR) << "System call returned " << result << ": " \
    << std::strerror(errno) << " or " << std::strerror(-result);                                 \
} while(0)

#define ASSERT(expr, msg) do { \
    if (__builtin_expect(!(expr), 0)) LOG(ERROR) << msg;\
} while(0)

void InitLogger();

#endif //SORTING_LOGGER_H
