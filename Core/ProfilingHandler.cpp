#include "ProfilingHandler.h"
#include "Logger.h"

#include <cstdio>
#include <cstdlib>
#include <typeinfo>

#ifdef __GNUC__
#include <cxxabi.h>
#endif

namespace {

std::string handlerTypeName(IPipelineHandler* h) {
    const char* raw = typeid(*h).name();
#ifdef __GNUC__
    int status = 0;
    char* demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
    if (status == 0 && demangled) {
        std::string result(demangled);
        std::free(demangled);
        return result;
    }
#endif
    return raw;
}

} // namespace

ProfilingHandler::ProfilingHandler(IPipelineHandler* inner)
    : inner_(inner), name_(handlerTypeName(inner)) {}

ProfilingHandler::~ProfilingHandler() {
    reportSummary();
}

void ProfilingHandler::processBlock(const float* iq, int count, double sampleRateHz) {
    const auto t0 = Clock::now();
    inner_->processBlock(iq, count, sampleRateHz);
    record(t0, count, sampleRateHz);
}

void ProfilingHandler::processBlock(const float* iq, int count, double sampleRateHz,
                                    const BlockMeta& meta) {
    const auto t0 = Clock::now();
    inner_->processBlock(iq, count, sampleRateHz, meta);
    record(t0, count, sampleRateHz);
}

void ProfilingHandler::onStreamStarted(double sampleRateHz) {
    total_       = {};
    window_      = {};
    windowStart_ = Clock::now();
    inner_->onStreamStarted(sampleRateHz);
}

void ProfilingHandler::onStreamStopped() {
    inner_->onStreamStopped();
    reportSummary();
}

void ProfilingHandler::onRetune(double newFreqHz) {
    inner_->onRetune(newFreqHz);
}

void ProfilingHandler::record(Clock::time_point t0, int count, double sampleRateHz) {
    const auto   now      = Clock::now();
    const double ms       = std::chrono::duration<double, std::milli>(now - t0).count();
    const double budgetMs = sampleRateHz > 0.0 ? count * 1000.0 / sampleRateHz : 0.0;
    const bool   over     = budgetMs > 0.0 && ms > budgetMs;

    for (Stats* s : {&total_, &window_}) {
        ++s->blocks;
        s->sumMs       += ms;
        s->sumBudgetMs += budgetMs;
        if (ms > s->maxMs) s->maxMs = ms;
        if (over) ++s->overBudget;
    }

    if (now - windowStart_ < std::chrono::seconds(1))
        return;

    if (window_.overBudget > 0) {
        const double windowSec = std::chrono::duration<double>(now - windowStart_).count();
        char buf[160];
        std::snprintf(buf, sizeof buf,
                      ": %llu of %llu blocks over budget in last %.1f s, worst %.2f ms / budget %.2f ms",
                      static_cast<unsigned long long>(window_.overBudget),
                      static_cast<unsigned long long>(window_.blocks),
                      windowSec, window_.maxMs, budgetMs);
        LOG_CAT(LogCat::kPipelineTiming, LogLevel::Warning, name_ + buf);
    }
    window_      = {};
    windowStart_ = now;
}

void ProfilingHandler::reportSummary() {
    if (total_.blocks == 0)
        return;

    const double avgMs     = total_.sumMs / static_cast<double>(total_.blocks);
    const double avgBudget = total_.sumBudgetMs / static_cast<double>(total_.blocks);
    const double loadPct   = avgBudget > 0.0 ? 100.0 * avgMs / avgBudget : 0.0;

    char buf[200];
    std::snprintf(buf, sizeof buf,
                  ": avg %.3f ms, max %.2f ms, budget %.2f ms (%.0f%%), %llu blocks, %llu over budget",
                  avgMs, total_.maxMs, avgBudget, loadPct,
                  static_cast<unsigned long long>(total_.blocks),
                  static_cast<unsigned long long>(total_.overBudget));
    LOG_CAT(LogCat::kPipelineTiming, LogLevel::Info, name_ + buf);

    total_  = {};
    window_ = {};
}
