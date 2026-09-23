#pragma once

#include "DspUtils.h"

#include <QVector>
#include <cmath>
#include <complex>
#include <vector>

// ---------------------------------------------------------------------------
// Default FIR tap counts — shared by FM and AM subclasses.
// Debug uses 31 taps to avoid USB FIFO overflow at high sample rates.
// ---------------------------------------------------------------------------
#ifndef NDEBUG
inline constexpr int kDefaultFir1Taps = 31;
#else
inline constexpr int kDefaultFir1Taps = 255;
#endif
inline constexpr int kDefaultFir2Taps = 255;
// SSB/CW narrow channel-select decimator (replaces FIR2 in those modems).
inline constexpr int kDefaultChanTaps = 255;

// User-configurable tap-count limits (odd counts only — symmetric linear phase).
inline constexpr int kMinFirTaps = 15;
inline constexpr int kMaxFirTaps = 1023;

// ---------------------------------------------------------------------------
// ChannelModem — common DSP pipeline for all demodulators.
//
//   float32 I/Q  →  DC blocker  →  NCO shift
//              →  k × halfband ÷2 (complex, 47 taps, only non-zero taps computed)
//              →  FIR1 LPF (complex) @ inputSR/2^k  →  decimate D1/2^k
//              →  IF @ ~500 kHz (exactly 480 kHz when inputSR allows → 48 kHz audio)
//              →  [virtual demodulateIF]  (every IF sample — stateful)
//              →  FIR2 LPF (real, dot product only on output samples)
//              →  decimate D2 = 10  →  audio @ ~48–53 kHz
//
// D1 = total IF decimation (halfbands × FIR1 decimation); FIR1 designs at the
// reduced rate, so the same tap count gives a 2^k-times narrower transition.
//
// Subclasses implement demodulateIF() — the only stage that differs:
//   FM: discriminator + de-emphasis
//   AM: envelope + DC removal
//   SSB/NFM/CW: future
//
// Thread safety: call all methods from the SAME thread (RxWorker thread).
// ---------------------------------------------------------------------------
class ChannelModem {
public:
    virtual ~ChannelModem() = default;

    [[nodiscard]] QVector<float> pushBlock(const float* iq, int count);
    void setOffset(double offsetHz);

    [[nodiscard]] double audioSampleRate() const { return audioSR_; }
    [[nodiscard]] double ifSampleRate()    const { return ifSR_;    }
    [[nodiscard]] int    decimation1()     const { return D1_;      }
    [[nodiscard]] double bandwidth()       const { return bandwidth_; }

protected:
    ChannelModem(double inputSR, double stationOffsetHz,
                 double fir1CutoffHz, double fir2CutoffHz,
                 double minIfHz,
                 int fir1Taps = kDefaultFir1Taps,
                 int fir2Taps = kDefaultFir2Taps);

    // Subclass implements: demodulate one IF-rate sample → audio sample.
    // ifSample: complex signal after FIR1 + D1 decimation.
    // ifPower:  |ifSample|².
    virtual double demodulateIF(std::complex<double> ifSample, double ifPower) = 0;

    // Called after base resets state in setOffset(). Override to reset
    // subclass-specific state (discriminator, de-emphasis, etc.).
    virtual void resetDemodState() {}

    // Audio production hook. Default path: demodulateIF() → FIR2 → decimate D2.
    // SSB/CW override this to run their own complex decimation + audio filter,
    // because the base FIR2 / D2 machinery is real-valued and private.
    //   ifSample: complex signal after FIR1 + D1 decimation (IF rate).
    //   ifPower:  |ifSample|².
    //   out:      audio samples (0 or more) are appended here.
    virtual void produceAudio(std::complex<double> ifSample, double ifPower,
                              QVector<float>& out);

    // Subclass name for log messages.
    virtual const char* modemName() const = 0;

    // Subclass tools — redesign filters on the fly.
    void redesignFir1(double cutoffHz);
    [[nodiscard]] double clampFir1Cutoff(double cutoffHz) const;
    void redesignFir2(double cutoffHz);

    // Accessible by subclass
    double inputSR_;
    double ifSR_;
    double audioSR_;
    int    D1_;
    double bandwidth_;   // user-facing bandwidth (meaning depends on subclass)

private:
    double stationOffset_;
    int    D2_{10};
    int    fir1Taps_;
    int    fir2Taps_;

    // ── DSP blocks ───────────────────────────────────────────────────────────
    dsp::DcBlocker    dc_;
    dsp::Nco          nco_;

    // ── Halfband ÷2 cascade (complex) ────────────────────────────────────────
    // Mirrored delay line; only the non-zero taps (every other one + centre)
    // are stored, and the output is computed on every 2nd input only.
    struct HalfbandStage {
        std::vector<int>                  idx;     // tap positions with c ≠ 0
        std::vector<double>               coef;
        std::vector<std::complex<double>> delay;   // 2 × kHalfbandTaps
        int  head{0};
        bool odd{false};
        // Returns true and writes y when a decimated output is ready.
        bool push(std::complex<double> x, std::complex<double>& y);
        void reset();
    };
    static constexpr int kHalfbandTaps = 47;   // 4m+3 → true halfband, ~−74 dB
    std::vector<HalfbandStage> hb_;
    double stageSR_;      // rate at FIR1 input = inputSR / 2^k
    int    D1r_{1};       // FIR1's own decimation = D1 / 2^k

    // ── Stage-1 FIR (complex) ────────────────────────────────────────────────
    std::vector<double>               fir1Coeffs_;
    std::vector<std::complex<double>> fir1Delay_;
    int                               fir1Head_{0};
    int                               dec1Counter_{0};

    // ── Stage-2 FIR (real) ───────────────────────────────────────────────────
    std::vector<double> fir2Coeffs_;
    std::vector<double> fir2Delay_;
    int                 fir2Head_{0};
    int                 dec2Counter_{0};

    void                 fir1Push(std::complex<double> x);
    std::complex<double> fir1Compute() const;
    void                 fir2Push(double x);
    double               fir2Compute() const;
};
