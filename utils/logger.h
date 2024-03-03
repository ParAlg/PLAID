//
// Created by peter on 3/2/24.
//

#ifndef SORTING_LOGGER_H
#define SORTING_LOGGER_H

#include <cerrno>
#include <cstring>
#include "absl/log/log.h"
#include "absl/log/initialize.h"

#define SYSCALL(expr) do { \
    int result = (expr);   \
    if (__builtin_expect(result < 0, 0)) LOG(ERROR) << "System call error: " << std::strerror(errno); \
} while(0)

void InitLogger() {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        absl::InitializeLog();
    }
}

#endif //SORTING_LOGGER_H
