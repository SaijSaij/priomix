// SPDX-License-Identifier: GPL-3.0-only
#include "logging.hpp"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace priomix::log {

bool DebugEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("PRIOMIX_DEBUG");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

void Write(Level level, const char* fmt, ...) {
    const char* label = "INFO ";
    FILE* out = stdout;
    switch (level) {
        case Level::Error:
            label = "ERROR";
            out = stderr;
            break;
        case Level::Warn:
            label = "WARN ";
            out = stderr;
            break;
        case Level::Info:
            label = "INFO ";
            break;
        case Level::Debug:
            label = "DEBUG";
            break;
    }

    // PipeWire's default timestamp type is "local" (wall clock).
    char stamp[16] = "--:--:--";
    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) != nullptr) {
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
    }

    std::fprintf(out, "%s %s priomix | ", label, stamp);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(out, fmt, args);
    va_end(args);
    std::fputc('\n', out);
    std::fflush(out);
}

}  // namespace priomix::log
