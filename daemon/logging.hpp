// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Minimal logging modelled on PipeWire/WirePlumber output conventions:
//   "<LEVEL> HH:MM:SS priomix | message"
// Errors and warnings go to stderr, info and debug to stdout. Debug output is opt-in via
// PRIOMIX_DEBUG (any non-empty value other than "0").

#include <cstdio>

namespace priomix::log {

enum class Level { Error, Warn, Info, Debug };

bool DebugEnabled();

[[gnu::format(printf, 2, 3)]] void Write(Level level, const char* fmt, ...);

}  // namespace priomix::log

#define PRIOMIX_LOG_ERROR(...) \
    ::priomix::log::Write(::priomix::log::Level::Error, __VA_ARGS__)
#define PRIOMIX_LOG_WARN(...) \
    ::priomix::log::Write(::priomix::log::Level::Warn, __VA_ARGS__)
#define PRIOMIX_LOG_INFO(...) \
    ::priomix::log::Write(::priomix::log::Level::Info, __VA_ARGS__)
#define PRIOMIX_LOG_DEBUG(...)                                                \
    do {                                                                      \
        if (::priomix::log::DebugEnabled())                                   \
            ::priomix::log::Write(::priomix::log::Level::Debug, __VA_ARGS__); \
    } while (0)
