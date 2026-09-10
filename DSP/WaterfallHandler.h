#pragma once

#include "../Core/IPipelineHandler.h"
#include "../Core/WaterfallSettings.h"

#include <QObject>
#include <QVector>
#include <QMetaType>

#include <atomic>
#include <vector>

#include <fftw3.h>

// ---------------------------------------------------------------------------
// WaterfallLine — one finished waterfall row.
// powerDb: fftSize values, FFT-shifted (DC in the centre), dBFS.
// ---------------------------------------------------------------------------
struct WaterfallLine {
    QVector<float> powerDb;
    double         centerFreqMHz = 0.0;
    double         sampleRateHz  = 0.0;
};
Q_DECLARE_METATYPE(WaterfallLine)

// ---------------------------------------------------------------------------
// WaterfallHandler — pipeline handler that FFTs every incoming sample into
// waterfall lines. Gap-free by construction: a carry buffer holds the partial
// FFT window across block boundaries, so each sample lands in exactly one
// contiguous non-overlapping N-sample window. Multiple FFTs per line are
// collapsed (max-hold or average) in the linear power domain; dB conversion
// happens once, at line emit.
//
// Threading: setters are called from the GUI thread and only store atomics;
// all DSP state is touched exclusively inside processBlock(), whose calls the
// Pipeline serializes per handler. lineReady() is emitted from the DSP pool
// thread — connect with Qt::QueuedConnection. The Qt event queue then acts as
// the unbounded render queue: if the GUI lags, lines render late, never lost.
// ---------------------------------------------------------------------------
class WaterfallHandler : public QObject, public IPipelineHandler {
    Q_OBJECT
public:
    explicit WaterfallHandler(QObject* parent = nullptr);
    ~WaterfallHandler() override;

    // GUI-thread setters; consumed by processBlock() on the DSP pool thread.
    void setEnabled(bool on)                              { enabled_.store(on, std::memory_order_relaxed); }
    void setFftSize(int n)                                { pendingFftSize_.store(n, std::memory_order_relaxed); }
    void setFps(int fps)                                  { fps_.store(fps, std::memory_order_relaxed); }
    void setAggregation(WaterfallSettings::Aggregation a) { aggregation_.store(int(a), std::memory_order_relaxed); }
    void setCenterFrequency(double mhz)                   { centerFreqMHz_.store(mhz, std::memory_order_relaxed); }
    void requestReset()                                   { resetRequested_.store(true, std::memory_order_relaxed); }

    // IPipelineHandler
    void processBlock(const float* iq, int count, double sampleRateHz) override;
    void onStreamStarted(double sampleRateHz) override;
    void onStreamStopped() override;
    void onRetune(double newFreqHz) override;

signals:
    // Emitted from the DSP thread; connect Queued. The Qt event queue is the
    // render queue: if the GUI lags, lines pile up and render late — never lost.
    void lineReady(WaterfallLine line);

private:
    void reconfigure(int n);
    void resetAccumulation();
    void runFft(const float* iqWindow);
    void finishLineIfDue();

    std::atomic<bool>   enabled_{true};
    std::atomic<int>    pendingFftSize_{4096};
    std::atomic<int>    fps_{25};
    std::atomic<int>    aggregation_{int(WaterfallSettings::Aggregation::MaxHold)};
    std::atomic<double> centerFreqMHz_{0.0};
    std::atomic<bool>   resetRequested_{false};

    // DSP state — touched only inside processBlock (per-handler calls
    // serialized by the Pipeline).
    int                fftSize_ = 0;   // current plan size (0 = no plan)
    fftwf_complex*     in_   = nullptr;
    fftwf_complex*     out_  = nullptr;
    fftwf_plan         plan_ = nullptr;
    std::vector<float> window_;
    double             normSq_ = 1.0;
    std::vector<float> carry_;         // partial window across blocks (interleaved I/Q)
    std::vector<float> accumPower_;    // per-bin linear power (max or sum)
    int                fftsInLine_    = 0;
    long long          samplesInLine_ = 0;
    double             sampleRateHz_  = 0.0;
};
