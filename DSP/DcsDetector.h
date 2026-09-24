#pragma once

#include "AudioProcessor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// DcsDetector — DCS (Digital-Coded Squelch, DPL) detector and code squelch
// for NFM audio.
//
// DCS sends a 23-bit Golay (23,12) codeword over and over, NRZ at 134.4 bit/s,
// LSB first: bits 0..8 = the octal code, bits 9..11 = fixed "100" (data word
// 0x800 | code), bits 12..22 = parity, generator x^11+x^10+x^6+x^5+x^4+x^2+1
// (0xC75). Inverted polarity ("I" codes) sends the complement.
//
//   audio → LPF ~300 Hz + decimate to ~1.3 kHz → subtract one-word moving
//           average → slicer → integrate-and-dump bits, clock pulled to the
//           slicer edges → 23-bit window matched against all 23 rotations of
//           the standard codes, ≤ kMaxBitErrors bit errors, confirmed by the
//           window one word earlier matching the same rotation.
//
// The Golay code is cyclic and contains the all-ones word, so the complement
// of a codeword is a rotation of another codeword: every standard inverted
// code is on air identical to a standard normal code (023I ≡ 047N, see
// invertedAlias()). The detector reports the normal code of the pair.
//
// Audio is passed through unchanged, or zeroed when a "DCS" target is set and
// the detected code differs. Place it before CtcssDetector (whose 300 Hz HPF
// removes the DCS band from the audio).
// ---------------------------------------------------------------------------
class DcsDetector : public IAudioProcessor {
public:
    static constexpr int kNumCodes = 104;
    // Standard codes as octal numbers (023 → 023 = 19).
    static const std::array<int, kNumCodes> kCodes;

    static constexpr double kBitRate         = 134.4;
    static constexpr double kDecimatedRateHz = 10.0 * kBitRate;
    static constexpr double kLowpassHz       = 300.0;
    static constexpr int    kMaxBitErrors    = 2;
    static constexpr double kHoldSec         = 0.5;   // detection lifetime w/o confirmation
    static constexpr double kClockGain       = 0.1;   // bit-clock pull per slicer edge

    // 23-bit codeword (bit 0 is sent first) for a 9-bit code.
    [[nodiscard]] static uint32_t encode(int code);
    // Standard code whose inverted transmission equals `code` sent normally
    // (and vice versa; 023 ↔ 047), or 0 if none.
    [[nodiscard]] static int invertedAlias(int code);
    // "047N = 023I" for a detected code; empty for 0.
    [[nodiscard]] static QString codeName(int code);

    void prepare(double sampleRateHz) override;
    void process(float* samples, int count) override;
    void reset() override;
    // "DCS": target code (octal value, e.g. 19 for 023), 0 = off. Also opens
    // on its inverted alias, which is the same signal on air.
    bool setParam(const QString& name, double value) override;

    // Last detected code (normal reading), 0 = none. Safe from any thread.
    [[nodiscard]] int detectedCode() const { return detected_.load(std::memory_order_relaxed); }
    [[nodiscard]] int targetCode() const { return target_; }

private:
    void onDecimated(double y);
    void onBit(int bit);

    double sr_{0.0};
    int    decim_{1};
    int    target_{0};
    int    targetAlias_{0};

    // Anti-alias LPF, evaluated only at decimation points.
    std::vector<double> lpf_;
    std::vector<double> ring_;
    int ringHead_{0};
    int phase_{0};

    // One-word moving average (the word's mean is constant → exact DC).
    std::vector<double> avgBuf_;
    int    avgPos_{0};
    double avgSum_{0.0};

    // Bit clock: phase in bits; a bit ends (and is dumped) when the phase
    // wraps, and slicer edges pull the wrap onto themselves.
    double bitStep_{0.0};
    double bitPhase_{0.0};
    double bitAcc_{0.0};
    int    lastSign_{0};

    uint64_t bits_{0};      // last 46 bits, shifted in at bit 45 (oldest at bit 0)
    int      bitCount_{0};
    int      holdBits_{0};  // bits left before the detection expires

    std::atomic<int> detected_{0};
};
