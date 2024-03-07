#include "utils/logger.h"

void InitLogger() {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        absl::InitializeLog();
    }
}