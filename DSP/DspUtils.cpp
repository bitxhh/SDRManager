#include "DspUtils.h"

namespace dsp {

std::vector<double> designLowpassFir(int numTaps, double cutoffNorm) {
    std::vector<double> h(numTaps);
    const int M   = numTaps - 1;
    const int mid = M / 2;

    for (int n = 0; n < numTaps; ++n) {
        double sinc;
        if (n == mid) {
            sinc = 2.0 * cutoffNorm;
        } else {
            const double x = 2.0 * kPi * cutoffNorm * (n - mid);
            sinc = std::sin(x) / (kPi * (n - mid));
        }
        const double win = 0.42
                         - 0.50 * std::cos(2.0 * kPi * n / M)
                         + 0.08 * std::cos(4.0 * kPi * n / M);
        h[n] = sinc * win;
    }

    double sum = 0.0;
    for (double v : h) sum += v;
    if (sum > 0.0)
        for (double& v : h) v /= sum;

    return h;
}

std::vector<double> designHilbertFir(int numTaps) {
    // Type III linear-phase FIR: antisymmetric, odd length → integer group delay.
    // Ideal impulse response h[m] = 2/(pi*m) for odd m, 0 for even m (m = n - mid).
    std::vector<double> h(numTaps, 0.0);
    const int M   = numTaps - 1;
    const int mid = M / 2;

    for (int n = 0; n < numTaps; ++n) {
        const int m = n - mid;
        double val = 0.0;
        if (m != 0 && (m % 2 != 0))
            val = 2.0 / (kPi * m);
        const double win = 0.42
                         - 0.50 * std::cos(2.0 * kPi * n / M)
                         + 0.08 * std::cos(4.0 * kPi * n / M);
        h[n] = val * win;
    }

    return h;
}

std::vector<double> designBandpassFir(int numTaps, double centerNorm,
                                      double halfBwNorm) {
    // Cosine-modulate a real lowpass prototype up to centerNorm.
    // The factor 2 restores unity gain in the (single-sided) passband.
    const std::vector<double> lp = designLowpassFir(numTaps, halfBwNorm);
    const int M   = numTaps - 1;
    const int mid = M / 2;

    std::vector<double> h(numTaps);
    for (int n = 0; n < numTaps; ++n)
        h[n] = 2.0 * lp[n] * std::cos(2.0 * kPi * centerNorm * (n - mid));

    return h;
}

// ---------------------------------------------------------------------------
// NoiseBlanker
// ---------------------------------------------------------------------------
void NoiseBlanker::configure(double sampleRateHz, double threshold, double widthSec) {
    constexpr double kRampSec = 2e-6;    // fade in/out (= look-ahead)
    constexpr double kAvgSec  = 5e-3;    // power average time constant
    threshold_ = threshold;
    ramp_  = std::max(2, static_cast<int>(std::lround(sampleRateHz * kRampSec)));
    width_ = std::max(1, static_cast<int>(std::lround(sampleRateHz * widthSec)));
    step_  = 1.0 / ramp_;
    alpha_ = 1.0 / std::max(1.0, sampleRateHz * kAvgSec);
    warmLen_ = static_cast<int>(std::lround(1.0 / alpha_));   // one time constant
    delay_.assign(ramp_, {0.0, 0.0});
    reset();
}

void NoiseBlanker::reset() {
    std::fill(delay_.begin(), delay_.end(), std::complex<double>{0.0, 0.0});
    head_ = 0;
    hold_ = 0;
    gain_ = 1.0;
    avg_  = 0.0;
    count_ = 0;
    blanked_ = 0;
}

} // namespace dsp
