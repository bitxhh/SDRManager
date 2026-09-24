#include "CtcssDetector.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

const std::array<double, CtcssDetector::kNumTones> CtcssDetector::kTones = {
     67.0,  69.3,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,
     94.8,  97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3,
    131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2, 165.5, 167.9,
    171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5,
    203.5, 206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1,
};

void CtcssDetector::prepare(double sampleRateHz) {
    sr_    = sampleRateHz;
    decim_ = std::max(1, static_cast<int>(std::lround(sr_ / kDecimatedRateHz)));
    const double fs2 = sr_ / decim_;

    // Passband to ~300 Hz, stopband from ~1 kHz (aliases only fold into the
    // tone band from fs2 − 254 Hz upward). Hamming: taps ≈ 3.3·fs/Δf.
    int taps = static_cast<int>(std::ceil(3.3 * sr_ / 700.0)) | 1;
    taps = std::clamp(taps, 31, 1023);
    lpf_ = dsp::designLowpassFir(taps, 400.0 / sr_);
    ring_.assign(lpf_.size(), 0.0);

    for (int k = 0; k < kNumTones; ++k)
        coeff_[k] = 2.0 * std::cos(2.0 * dsp::kPi * kTones[k] / fs2);
    winLen_ = std::max(1, static_cast<int>(std::lround(kWindowSec * fs2)));

    // 6th-order Butterworth HPF = 3 RBJ biquads with Q = 1/(2·sin((2k−1)π/12)).
    const double w0 = 2.0 * dsp::kPi * kHighpassHz / sr_;
    const double cw = std::cos(w0), sw = std::sin(w0);
    for (int k = 0; k < 3; ++k) {
        const double q     = 1.0 / (2.0 * std::sin((2 * k + 1) * dsp::kPi / 12.0));
        const double alpha = sw / (2.0 * q);
        const double a0    = 1.0 + alpha;
        Biquad& b = hpf_[k];
        b.b0 = (1.0 + cw) / 2.0 / a0;
        b.b1 = -(1.0 + cw) / a0;
        b.b2 = b.b0;
        b.a1 = -2.0 * cw / a0;
        b.a2 = (1.0 - alpha) / a0;
    }
    reset();
}

void CtcssDetector::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0);
    ringHead_ = 0;
    phase_    = 0;
    s1_.fill(0.0);
    s2_.fill(0.0);
    winPos_ = 0;
    winSum_ = winSumSq_ = 0.0;
    misses_ = 0;
    for (auto& b : hpf_) b.z1 = b.z2 = 0.0;
    detected_.store(0.0, std::memory_order_relaxed);
}

bool CtcssDetector::setParam(const QString& name, double value) {
    if (name != QLatin1String("CTCSS")) return false;
    target_ = std::max(0.0, value);
    return true;
}

void CtcssDetector::process(float* samples, int count) {
    if (sr_ <= 0.0) return;
    const int n = static_cast<int>(ring_.size());

    for (int i = 0; i < count; ++i) {
        ring_[ringHead_] = samples[i];
        if (++phase_ >= decim_) {
            phase_ = 0;
            // FIR output at this decimation point (newest sample at ringHead_).
            double y = 0.0;
            int idx = ringHead_;
            for (int k = 0; k < n; ++k) {
                y += lpf_[k] * ring_[idx];
                idx = (idx == 0) ? n - 1 : idx - 1;
            }
            for (int k = 0; k < kNumTones; ++k) {
                const double s = y + coeff_[k] * s1_[k] - s2_[k];
                s2_[k] = s1_[k];
                s1_[k] = s;
            }
            winSum_   += y;
            winSumSq_ += y * y;
            if (++winPos_ >= winLen_) analyzeWindow();
        }
        ringHead_ = (ringHead_ + 1) % n;
    }

    const bool mute = target_ > 0.0
        && std::abs(detectedToneHz() - target_) > kMatchTolHz;
    for (int i = 0; i < count; ++i) {
        double x = mute ? 0.0 : samples[i];
        for (auto& b : hpf_) x = b.process(x);
        samples[i] = static_cast<float>(x);
    }
}

void CtcssDetector::analyzeWindow() {
    const double N      = winLen_;
    const double energy = winSumSq_ - winSum_ * winSum_ / N;   // DC removed

    int    best = -1;
    double bestPow = 0.0;
    for (int k = 0; k < kNumTones; ++k) {
        const double p = s1_[k] * s1_[k] + s2_[k] * s2_[k] - coeff_[k] * s1_[k] * s2_[k];
        if (p > bestPow) { bestPow = p; best = k; }
    }
    // Fraction of the window energy in the best bin: 1.0 for a pure tone.
    const double frac = energy > 1e-20 ? 2.0 * bestPow / (N * energy) : 0.0;

    if (best >= 0 && frac >= kDetectFraction) {
        detected_.store(kTones[best], std::memory_order_relaxed);
        misses_ = 0;
    } else if (++misses_ >= 2) {
        detected_.store(0.0, std::memory_order_relaxed);
    }

    s1_.fill(0.0);
    s2_.fill(0.0);
    winPos_ = 0;
    winSum_ = winSumSq_ = 0.0;
}
