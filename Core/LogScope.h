#pragma once

#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

// RAII-замер длительности операции с логом в категорию `cat`.
//
//   void LimeDevice::init(...) {
//       LogScope scope(LogCat::kDeviceLifecycle, "init " + serial_);
//       ...
//   }
//
// На выходе из scope пишет "init XXX done in 812.3 ms" (Info), а если выход
// произошёл из-за исключения — "init XXX FAILED after 40.1 ms" (Error).
// Если категория выключена, в деструкторе только проверка флага.
// Не для hot path: std::string строится в конструкторе всегда.
class LogScope {
public:
    LogScope(const char* cat, std::string what)
        : cat_(cat),
          what_(std::move(what)),
          exceptions_(std::uncaught_exceptions()),
          t0_(std::chrono::steady_clock::now()) {}

    LogScope(const LogScope&)            = delete;
    LogScope& operator=(const LogScope&) = delete;

    ~LogScope() {
        const bool failed = std::uncaught_exceptions() > exceptions_;
        LOG_CAT(cat_, failed ? LogLevel::Error : LogLevel::Info,
                what_ + (failed ? " FAILED after " : " done in ") + elapsedMs() + " ms");
    }

private:
    std::string elapsedMs() const {
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0_).count();
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f", ms);
        return buf;
    }

    const char*                           cat_;
    std::string                           what_;
    int                                   exceptions_;
    std::chrono::steady_clock::time_point t0_;
};
