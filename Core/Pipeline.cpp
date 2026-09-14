#include "Pipeline.h"
#include "LoggerConfig.h"

#include <QtConcurrent/QtConcurrent>
#include <algorithm>

Pipeline::Pipeline(QThreadPool* pool, QObject* parent)
    : QObject(parent), pool_(pool) {}

void Pipeline::addHandler(IPipelineHandler* handler) {
    std::unique_lock lock(mutex_);
    if (LoggerConfig::instance().isEnabled(QLatin1String(LogCat::kPipelineTiming))) {
        profilers_.push_back(std::make_unique<ProfilingHandler>(handler));
        handlers_.push_back(profilers_.back().get());
    } else {
        handlers_.push_back(handler);
    }
}

void Pipeline::removeHandler(IPipelineHandler* handler) {
    std::unique_lock lock(mutex_);
    auto isTarget = [handler](IPipelineHandler* h) {
        if (h == handler) return true;
        auto* p = dynamic_cast<ProfilingHandler*>(h);
        return p && p->inner() == handler;
    };
    handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(), isTarget),
                    handlers_.end());
    // Обёртки уничтожаются после удаления из handlers_ — dispatch их уже не видит.
    profilers_.erase(std::remove_if(profilers_.begin(), profilers_.end(),
                                    [handler](const auto& p) { return p->inner() == handler; }),
                     profilers_.end());
}

void Pipeline::clearHandlers() {
    std::unique_lock lock(mutex_);
    handlers_.clear();
    profilers_.clear();
}

void Pipeline::dispatchBlock(const float* iq, int count, double sampleRateHz) {
    // shared_lock позволяет параллельные dispatch, но блокирует
    // add/remove/clear до завершения — так delete handler'а в teardown
    // не произойдёт, пока processBlock ещё работает.
    std::shared_lock lock(mutex_);
    if (!pool_ || handlers_.size() <= 1) {
        for (auto* h : handlers_)
            h->processBlock(iq, count, sampleRateHz);
        return;
    }
    QList<QFuture<void>> futures;
    futures.reserve(static_cast<qsizetype>(handlers_.size()));
    for (auto* h : handlers_)
        futures << QtConcurrent::run(pool_, [=] { h->processBlock(iq, count, sampleRateHz); });
    for (auto& f : futures)
        f.waitForFinished();
}

void Pipeline::dispatchBlock(const float* iq, int count, double sampleRateHz,
                              const BlockMeta& meta) {
    std::shared_lock lock(mutex_);
    if (!pool_ || handlers_.size() <= 1) {
        for (auto* h : handlers_)
            h->processBlock(iq, count, sampleRateHz, meta);
        return;
    }
    QList<QFuture<void>> futures;
    futures.reserve(static_cast<qsizetype>(handlers_.size()));
    for (auto* h : handlers_)
        futures << QtConcurrent::run(pool_, [=] { h->processBlock(iq, count, sampleRateHz, meta); });
    for (auto& f : futures)
        f.waitForFinished();
}

void Pipeline::notifyStarted(double sampleRateHz) {
    std::shared_lock lock(mutex_);
    for (auto* h : handlers_)
        h->onStreamStarted(sampleRateHz);
}

void Pipeline::notifyStopped() {
    std::shared_lock lock(mutex_);
    for (auto* h : handlers_)
        h->onStreamStopped();
}

void Pipeline::notifyRetune(double newFreqHz) {
    std::shared_lock lock(mutex_);
    for (auto* h : handlers_)
        h->onRetune(newFreqHz);
}
