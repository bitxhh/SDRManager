#include "DcsDetector.h"
#include "DspUtils.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

const std::array<int, DcsDetector::kNumCodes> DcsDetector::kCodes = {
    0023, 0025, 0026, 0031, 0032, 0036, 0043, 0047, 0051, 0053, 0054, 0065, 0071, 0072,
    0073, 0074, 0114, 0115, 0116, 0122, 0125, 0131, 0132, 0134, 0143, 0145, 0152, 0155,
    0156, 0162, 0165, 0172, 0174, 0205, 0212, 0223, 0225, 0226, 0243, 0244, 0245, 0246,
    0251, 0252, 0255, 0261, 0263, 0265, 0266, 0271, 0274, 0306, 0311, 0315, 0325, 0331,
    0332, 0343, 0346, 0351, 0356, 0364, 0365, 0371, 0411, 0412, 0413, 0423, 0431, 0432,
    0445, 0446, 0452, 0454, 0455, 0462, 0464, 0465, 0466, 0503, 0506, 0516, 0523, 0526,
    0532, 0546, 0565, 0606, 0612, 0624, 0627, 0631, 0632, 0654, 0662, 0664, 0703, 0712,
    0723, 0731, 0732, 0734, 0743, 0754,
};

namespace {
constexpr uint32_t kWordMask = (1u << 23) - 1;
constexpr int      kHistBits = 46;   // two words

uint32_t rotr23(uint32_t w, int k) {
    return ((w >> k) | (w << (23 - k))) & kWordMask;
}

struct Entry { uint32_t word; int code; };

// All 23 rotations of every standard code. Rotations of distinct codes never
// coincide (checked for the standard set), so first-minimum search is exact.
const std::vector<Entry>& table() {
    static const std::vector<Entry> t = [] {
        std::vector<Entry> v;
        v.reserve(DcsDetector::kNumCodes * 23);
        for (int code : DcsDetector::kCodes) {
            const uint32_t w = DcsDetector::encode(code);
            for (int k = 0; k < 23; ++k) v.push_back({rotr23(w, k), code});
        }
        return v;
    }();
    return t;
}

// Index of the nearest table entry and its bit distance.
std::pair<int, int> nearest(uint32_t w) {
    const auto& t = table();
    int best = -1, bestD = 24;
    for (int i = 0; i < static_cast<int>(t.size()); ++i) {
        const int d = std::popcount(w ^ t[i].word);
        if (d < bestD) { bestD = d; best = i; }
    }
    return {best, bestD};
}
} // namespace

uint32_t DcsDetector::encode(int code) {
    const uint32_t data = 0x800u | (static_cast<uint32_t>(code) & 0x1FFu);
    uint32_t r = data << 11;
    for (int i = 22; i >= 11; --i)
        if ((r >> i) & 1u) r ^= 0xC75u << (i - 11);
    return data | ((r & 0x7FFu) << 12);
}

int DcsDetector::invertedAlias(int code) {
    const uint32_t inv = ~encode(code) & kWordMask;
    for (const Entry& e : table())
        if (e.word == inv) return e.code;
    return 0;
}

QString DcsDetector::codeName(int code) {
    if (code <= 0) return {};
    QString s = QString("%1N").arg(code, 3, 8, QChar('0'));
    if (const int alias = invertedAlias(code))
        s += QString(" = %1I").arg(alias, 3, 8, QChar('0'));
    return s;
}

void DcsDetector::prepare(double sampleRateHz) {
    sr_    = sampleRateHz;
    decim_ = std::max(1, static_cast<int>(std::lround(sr_ / kDecimatedRateHz)));
    const double fs2 = sr_ / decim_;

    // Passband to ~300 Hz, stopband by ~900 Hz (aliases fold into the DCS
    // band from fs2 − 300 Hz upward). Hamming: taps ≈ 3.3·fs/Δf.
    int taps = static_cast<int>(std::ceil(3.3 * sr_ / 600.0)) | 1;
    taps = std::clamp(taps, 31, 1023);
    lpf_ = dsp::designLowpassFir(taps, kLowpassHz / sr_);
    ring_.assign(lpf_.size(), 0.0);

    bitStep_ = kBitRate / fs2;
    avgBuf_.assign(std::max(1, static_cast<int>(std::lround(23.0 / bitStep_))), 0.0);
    reset();
}

void DcsDetector::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0);
    ringHead_ = 0;
    phase_    = 0;
    std::fill(avgBuf_.begin(), avgBuf_.end(), 0.0);
    avgPos_ = 0;
    avgSum_ = 0.0;
    bitPhase_ = bitAcc_ = 0.0;
    lastSign_ = 0;
    bits_     = 0;
    bitCount_ = holdBits_ = 0;
    detected_.store(0, std::memory_order_relaxed);
}

bool DcsDetector::setParam(const QString& name, double value) {
    if (name != QLatin1String("DCS")) return false;
    target_      = std::max(0, static_cast<int>(std::lround(value)));
    targetAlias_ = target_ > 0 ? invertedAlias(target_) : 0;
    return true;
}

void DcsDetector::process(float* samples, int count) {
    if (sr_ <= 0.0) return;
    const int n = static_cast<int>(ring_.size());

    for (int i = 0; i < count; ++i) {
        ring_[ringHead_] = samples[i];
        if (++phase_ >= decim_) {
            phase_ = 0;
            double y = 0.0;
            int idx = ringHead_;
            for (int k = 0; k < n; ++k) {
                y += lpf_[k] * ring_[idx];
                idx = (idx == 0) ? n - 1 : idx - 1;
            }
            onDecimated(y);
        }
        ringHead_ = (ringHead_ + 1) % n;
    }

    if (target_ > 0) {
        const int  det  = detectedCode();
        const bool open = det != 0 && (det == target_ || det == targetAlias_);
        if (!open) std::fill(samples, samples + count, 0.0f);
    }
}

void DcsDetector::onDecimated(double y) {
    // A whole word always has the same mean, so a one-word average is the
    // exact DC level of the (possibly offset) NRZ stream.
    const int na = static_cast<int>(avgBuf_.size());
    avgSum_ += y - avgBuf_[avgPos_];
    avgBuf_[avgPos_] = y;
    if (++avgPos_ >= na) {
        avgPos_ = 0;
        avgSum_ = 0.0;   // re-sum once per word to stop rounding drift
        for (double v : avgBuf_) avgSum_ += v;
    }
    const double v    = y - avgSum_ / na;
    const int    sign = v >= 0.0 ? 1 : -1;

    // Edge → pull the bit boundary (phase wrap) toward it.
    if (lastSign_ != 0 && sign != lastSign_) {
        const double err = bitPhase_ < 0.5 ? bitPhase_ : bitPhase_ - 1.0;
        bitPhase_ -= kClockGain * err;
    }
    lastSign_ = sign;

    bitPhase_ += bitStep_;
    if (bitPhase_ >= 1.0) {
        // The sample straddling the boundary belongs mostly to the new bit.
        bitPhase_ -= 1.0;
        const double over = bitPhase_ / bitStep_;
        onBit(bitAcc_ + v * (1.0 - over) > 0.0 ? 1 : 0);
        bitAcc_ = v * over;
    } else {
        bitAcc_ += v;
    }
}

void DcsDetector::onBit(int bit) {
    bits_ = (bits_ >> 1) | (static_cast<uint64_t>(bit) << (kHistBits - 1));
    if (bitCount_ < kHistBits) ++bitCount_;
    if (holdBits_ > 0 && --holdBits_ == 0)
        detected_.store(0, std::memory_order_relaxed);
    if (bitCount_ < kHistBits) return;

    // Newest word = bits 23..45, the previous one = bits 0..22. The stream
    // repeats every 23 bits, so both must match the same rotation. Inverted
    // transmissions need no separate search: they are rotations of other
    // standard codes.
    const uint32_t cur  = static_cast<uint32_t>(bits_ >> 23) & kWordMask;
    const uint32_t prev = static_cast<uint32_t>(bits_) & kWordMask;
    const auto [idx, d] = nearest(cur);
    if (idx < 0 || d > kMaxBitErrors) return;
    const Entry& e = table()[idx];
    if (std::popcount(prev ^ e.word) > kMaxBitErrors) return;
    detected_.store(e.code, std::memory_order_relaxed);
    holdBits_ = static_cast<int>(std::ceil(kHoldSec * kBitRate));
}
