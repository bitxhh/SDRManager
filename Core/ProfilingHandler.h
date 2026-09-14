#pragma once

#include "IPipelineHandler.h"

#include <chrono>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// ProfilingHandler — декоратор над IPipelineHandler, замеряющий processBlock
// относительно real-time бюджета блока (count / sampleRateHz).
//
// Pipeline оборачивает handler автоматически при addHandler(), если включена
// категория LogCat::kPipelineTiming. Пишет в эту категорию:
//   • Warning не чаще раза в секунду, если были блоки сверх бюджета;
//   • сводку (avg / max / доля бюджета) при onStreamStopped().
//
// Потоки: Pipeline запускает каждый handler одной задачей на блок и ждёт все,
// так что конкретный экземпляр в каждый момент трогает один поток — без
// синхронизации. Стоимость: два steady_clock::now() на блок, не на сэмпл.
// ---------------------------------------------------------------------------
class ProfilingHandler : public IPipelineHandler {
public:
    // inner не принадлежит обёртке.
    explicit ProfilingHandler(IPipelineHandler* inner);
    ~ProfilingHandler() override;

    IPipelineHandler*  inner() const { return inner_; }
    const std::string& name() const  { return name_; }

    void processBlock(const float* iq, int count, double sampleRateHz) override;
    void processBlock(const float* iq, int count, double sampleRateHz,
                      const BlockMeta& meta) override;
    void onStreamStarted(double sampleRateHz) override;
    void onStreamStopped() override;
    void onRetune(double newFreqHz) override;

private:
    using Clock = std::chrono::steady_clock;

    struct Stats {
        uint64_t blocks{0};
        uint64_t overBudget{0};
        double   sumMs{0.0};
        double   maxMs{0.0};
        double   sumBudgetMs{0.0};
    };

    void record(Clock::time_point t0, int count, double sampleRateHz);
    void reportSummary();

    IPipelineHandler* inner_;
    std::string       name_;

    Stats             total_;
    Stats             window_;           // с последнего Warning
    Clock::time_point windowStart_{Clock::now()};
};
