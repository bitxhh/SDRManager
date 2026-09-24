#pragma once

#include "AudioProcessor.h"

#include <array>
#include <atomic>
#include <vector>

// ---------------------------------------------------------------------------
// CtcssDetector — CTCSS (sub-audible tone) detector, tone squelch and 300 Hz
// highpass for NFM audio.
//
//   audio → LPF ~400 Hz + decimate to ~2 kHz → Goertzel bank (50 EIA tones,
//           0.4 s window ≈ 2.5 Hz bins) → detectedToneHz()
//   audio → [mute unless detected == "CTCSS" target] → 6th-order Butterworth
//           HPF 300 Hz (the tone isn't heard)
//
// A tone is detected when its bin holds ≥ kDetectFraction of the window's
// (DC-removed) energy; the result survives one missed window.
// Param "CTCSS": target tone in Hz, 0 = off (audio always passes).
// ---------------------------------------------------------------------------
class CtcssDetector : public IAudioProcessor {
public:
    static constexpr int kNumTones = 50;
    static const std::array<double, kNumTones> kTones;

    static constexpr double kDecimatedRateHz = 2'000.0;
    static constexpr double kWindowSec       = 0.4;
    static constexpr double kDetectFraction  = 0.3;
    static constexpr double kMatchTolHz      = 1.0;
    static constexpr double kHighpassHz      = 300.0;

    void prepare(double sampleRateHz) override;
    void process(float* samples, int count) override;
    void reset() override;
    bool setParam(const QString& name, double value) override;

    // Last detected tone (Hz), 0 = none. Safe to read from any thread.
    [[nodiscard]] double detectedToneHz() const { return detected_.load(std::memory_order_relaxed); }
    [[nodiscard]] double targetToneHz() const { return target_; }

private:
    struct Biquad {
        double b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
        double z1{0}, z2{0};
        double process(double x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void analyzeWindow();

    double sr_{0.0};
    int    decim_{1};
    double target_{0.0};

    // Anti-alias LPF, evaluated only at decimation points.
    std::vector<double> lpf_;
    std::vector<double> ring_;     // input history, size lpf_.size()
    int ringHead_{0};
    int phase_{0};

    // Goertzel bank over the decimated stream.
    std::array<double, kNumTones> coeff_{};
    std::array<double, kNumTones> s1_{}, s2_{};
    int    winLen_{0};
    int    winPos_{0};
    double winSum_{0.0}, winSumSq_{0.0};
    int    misses_{0};

    std::array<Biquad, 3> hpf_{};

    std::atomic<double> detected_{0.0};
};
