#include "WaterfallHandler.h"
#include "FftPlannerLock.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

WaterfallHandler::WaterfallHandler(QObject* parent) : QObject(parent) {
    qRegisterMetaType<WaterfallLine>("WaterfallLine");
}

WaterfallHandler::~WaterfallHandler() {
    if (plan_) {
        std::lock_guard<std::mutex> lock(fftwPlannerMutex());
        fftwf_destroy_plan(plan_);
    }
    if (in_)  fftwf_free(in_);
    if (out_) fftwf_free(out_);
}

void WaterfallHandler::onStreamStarted(double) { resetRequested_.store(true, std::memory_order_relaxed); }
void WaterfallHandler::onStreamStopped()       { resetRequested_.store(true, std::memory_order_relaxed); }

void WaterfallHandler::onRetune(double newFreqHz) {
    centerFreqMHz_.store(newFreqHz / 1e6, std::memory_order_relaxed);
    resetRequested_.store(true, std::memory_order_relaxed);
}

void WaterfallHandler::reconfigure(int n) {
    {
        std::lock_guard<std::mutex> lock(fftwPlannerMutex());
        if (plan_) fftwf_destroy_plan(plan_);
        if (in_)  fftwf_free(in_);
        if (out_) fftwf_free(out_);
        in_  = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * n));
        out_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * n));
        // FFTW_ESTIMATE, not FFTW_MEASURE: planning runs on the live DSP path
        // (first block after start / N change); MEASURE (~1 s) would stall the
        // pipeline barrier and overflow the SDR FIFO.
        plan_ = fftwf_plan_dft_1d(n, in_, out_, FFTW_FORWARD, FFTW_ESTIMATE);
    }

    window_.resize(n);
    float winSum = 0.0f;
    for (int i = 0; i < n; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(static_cast<float>(2.0 * kPi * i / (n - 1))));
        winSum += window_[i];
    }
    normSq_ = static_cast<double>(winSum) * static_cast<double>(winSum);

    fftSize_ = n;
    accumPower_.assign(n, 0.0f);
    carry_.clear();
    resetAccumulation();
}

void WaterfallHandler::resetAccumulation() {
    std::fill(accumPower_.begin(), accumPower_.end(), 0.0f);
    fftsInLine_    = 0;
    samplesInLine_ = 0;
}

void WaterfallHandler::processBlock(const float* iq, int count, double sampleRateHz) {
    if (!enabled_.load(std::memory_order_relaxed)) {
        carry_.clear();
        if (fftsInLine_ > 0 || samplesInLine_ > 0) resetAccumulation();
        return;
    }

    const int n = pendingFftSize_.load(std::memory_order_relaxed);
    if (n != fftSize_) reconfigure(n);

    if (resetRequested_.exchange(false, std::memory_order_relaxed)) {
        carry_.clear();
        resetAccumulation();
    }

    sampleRateHz_ = sampleRateHz;

    const float* src  = iq;
    int          left = count;                      // I/Q pairs remaining

    if (!carry_.empty()) {                          // 1) top up carried partial window
        const int need = fftSize_ - int(carry_.size() / 2);
        const int take = std::min(need, left);
        carry_.insert(carry_.end(), src, src + 2 * take);
        src  += 2 * take;
        left -= take;
        if (int(carry_.size() / 2) == fftSize_) {
            runFft(carry_.data());
            carry_.clear();
        }
    }

    while (left >= fftSize_) {                      // 2) full windows straight from the block — no copy
        runFft(src);
        src  += 2 * fftSize_;
        left -= fftSize_;
    }

    if (left > 0)                                   // 3) keep tail for the next block
        carry_.insert(carry_.end(), src, src + 2 * left);
}

void WaterfallHandler::runFft(const float* iqWindow) {
    const int n = fftSize_;
    for (int i = 0; i < n; ++i) {
        const float w = window_[i];
        in_[i][0] = iqWindow[2 * i]     * w;
        in_[i][1] = iqWindow[2 * i + 1] * w;
    }
    fftwf_execute(plan_);

    const bool  maxHold = aggregation_.load(std::memory_order_relaxed)
                          == int(WaterfallSettings::Aggregation::MaxHold);
    const float norm = float(1.0 / normSq_);
    const int   half = n / 2;
    for (int k = 0; k < n; ++k) {
        const int   src = (k + half) % n;           // FFT-shift: DC to the centre
        const float re  = out_[src][0];
        const float im  = out_[src][1];
        const float p   = (re * re + im * im) * norm;
        if (maxHold) {
            if (fftsInLine_ == 0 || p > accumPower_[k]) accumPower_[k] = p;
        } else {
            accumPower_[k] = (fftsInLine_ == 0) ? p : accumPower_[k] + p;
        }
    }
    ++fftsInLine_;
    samplesInLine_ += n;
    finishLineIfDue();
}

void WaterfallHandler::finishLineIfDue() {
    const int fps = std::max(1, fps_.load(std::memory_order_relaxed));
    const long long samplesPerLine =
        std::max<long long>(fftSize_, std::llround(sampleRateHz_ / fps));
    if (samplesInLine_ < samplesPerLine || fftsInLine_ == 0) return;

    WaterfallLine line;
    line.centerFreqMHz = centerFreqMHz_.load(std::memory_order_relaxed);
    line.sampleRateHz  = sampleRateHz_;
    line.powerDb.resize(fftSize_);

    const bool  average = aggregation_.load(std::memory_order_relaxed)
                          == int(WaterfallSettings::Aggregation::Average);
    const float scale = average ? 1.0f / float(fftsInLine_) : 1.0f;
    float* dst = line.powerDb.data();
    for (int k = 0; k < fftSize_; ++k)
        dst[k] = 10.0f * std::log10(accumPower_[k] * scale + 1e-12f);

    emit lineReady(line);

    // Keep the fractional remainder for an exact long-term rate; collapse any
    // backlog — data always lands in the next line, only the line count is
    // normalized after a stall.
    samplesInLine_ %= samplesPerLine;
    fftsInLine_ = 0;
    std::fill(accumPower_.begin(), accumPower_.end(), 0.0f);
}
