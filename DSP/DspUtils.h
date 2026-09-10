#pragma once

#include <algorithm>
#include <complex>
#include <vector>
#include <cmath>

namespace dsp {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Blackman-windowed sinc FIR lowpass design.
// cutoffNorm = fc / fs, range [0, 0.5].  Returns unity-gain normalised taps.
// ---------------------------------------------------------------------------
std::vector<double> designLowpassFir(int numTaps, double cutoffNorm);

// ---------------------------------------------------------------------------
// Blackman-windowed Hilbert transformer (Type III, odd taps → integer delay).
// Antisymmetric taps h[m] = 2/(pi*m) for odd m, 0 otherwise (m = n - mid).
// Group delay = (numTaps-1)/2 samples.  Use FirReal + DelayLine to pair I/Q.
// Convention: H{cos(wt)} = sin(wt),  H{sin(wt)} = -cos(wt).
// ---------------------------------------------------------------------------
std::vector<double> designHilbertFir(int numTaps);

// ---------------------------------------------------------------------------
// Blackman-windowed bandpass FIR = cosine-modulated lowpass prototype.
// centerNorm = fc/fs, halfBwNorm = (BW/2)/fs.  Passband = center ± halfBw.
// ---------------------------------------------------------------------------
std::vector<double> designBandpassFir(int numTaps, double centerNorm,
                                      double halfBwNorm);

// ---------------------------------------------------------------------------
// DC blocker — first-order IIR highpass for complex I/Q.
// Removes LO leakage (DC spike at 0 Hz in baseband).
// H(z) = (1 - z^-1) / (1 - alpha * z^-1)
// alpha = 0.9999 → cutoff ~32 Hz at 2 MHz SR.
// ---------------------------------------------------------------------------
struct DcBlocker {
    std::complex<double> prevIn{0.0, 0.0};
    std::complex<double> prevOut{0.0, 0.0};
    double alpha = 0.9999;

    std::complex<double> process(std::complex<double> s) {
        auto out = s - prevIn + alpha * prevOut;
        prevIn  = s;
        prevOut = out;
        return out;
    }

    void reset() {
        prevIn  = {0.0, 0.0};
        prevOut = {0.0, 0.0};
    }
};

// ---------------------------------------------------------------------------
// NCO — numerically controlled oscillator for frequency shifting.
// mix() multiplies input by e^{j*phase} and advances phase.
// ---------------------------------------------------------------------------
struct Nco {
    double phase    = 0.0;
    double phaseInc = 0.0;

    void setFrequency(double offsetHz, double sampleRate) {
        phaseInc = -2.0 * kPi * offsetHz / sampleRate;
    }

    std::complex<double> mix(std::complex<double> s) {
        auto result = s * std::complex<double>(std::cos(phase), std::sin(phase));
        phase += phaseInc;
        if (phase >  kPi) phase -= 2.0 * kPi;
        if (phase < -kPi) phase += 2.0 * kPi;
        return result;
    }

    void reset() { phase = 0.0; }
};

// ---------------------------------------------------------------------------
// First-order IIR highpass.
// H(z) = alpha * (y[n-1] + x[n] - x[n-1])
// alpha = exp(-2*pi*fc/fs)
// ---------------------------------------------------------------------------
struct IirHighpass1 {
    double alpha  = 0.0;
    double state  = 0.0;
    double prevIn = 0.0;

    void setCutoff(double fc, double fs) {
        alpha = std::exp(-2.0 * kPi * fc / fs);
    }

    double process(double x) {
        double out = alpha * (state + x - prevIn);
        state  = out;
        prevIn = x;
        return out;
    }

    void reset() {
        state  = 0.0;
        prevIn = 0.0;
    }
};

// ---------------------------------------------------------------------------
// FirReal — real FIR filter, direct convolution y[n] = Σ coeffs[k]·x[n-k].
// Newest sample pushed at head, taps read backward, so it produces the
// mathematically correct sign for antisymmetric (Hilbert) coefficients too.
// ---------------------------------------------------------------------------
struct FirReal {
    std::vector<double> coeffs;
    std::vector<double> delay;
    int head = 0;

    void setCoeffs(std::vector<double> c) {
        coeffs = std::move(c);
        delay.assign(coeffs.size(), 0.0);
        head = 0;
    }

    double process(double x) {
        const int n = static_cast<int>(coeffs.size());
        if (n == 0) return x;
        delay[head] = x;
        double acc = 0.0;
        int idx = head;
        for (int k = 0; k < n; ++k) {
            acc += coeffs[k] * delay[idx];
            idx = (idx == 0) ? n - 1 : idx - 1;   // read backward: x[n], x[n-1] …
        }
        head = (head + 1) % n;
        return acc;
    }

    void reset() {
        std::fill(delay.begin(), delay.end(), 0.0);
        head = 0;
    }
};

// ---------------------------------------------------------------------------
// DelayLine — integer sample delay (matches a FIR's group delay).
// ---------------------------------------------------------------------------
struct DelayLine {
    std::vector<double> buf;
    int head  = 0;
    int delay = 0;

    void setDelay(int d) {
        delay = std::max(0, d);
        buf.assign(delay + 1, 0.0);
        head = 0;
    }

    double process(double x) {
        const int L = static_cast<int>(buf.size());
        buf[head] = x;
        int rd = head - delay;
        if (rd < 0) rd += L;
        const double out = buf[rd];
        head = (head + 1) % L;
        return out;
    }

    void reset() {
        std::fill(buf.begin(), buf.end(), 0.0);
        head = 0;
    }
};

// ---------------------------------------------------------------------------
// FirComplexDecimator — complex FIR anti-alias filter + integer decimation.
// process() is called for every input sample; returns true (and writes `out`)
// only on decimation output points, computing the O(N) dot-product just then.
// ---------------------------------------------------------------------------
struct FirComplexDecimator {
    std::vector<double>               coeffs;
    std::vector<std::complex<double>> delay;
    int head    = 0;
    int decim   = 1;
    int counter = 0;

    void setup(std::vector<double> c, int decimation) {
        coeffs = std::move(c);
        delay.assign(coeffs.size(), std::complex<double>{0.0, 0.0});
        head = 0;
        decim = std::max(1, decimation);
        counter = 0;
    }

    bool process(std::complex<double> x, std::complex<double>& out) {
        const int n = static_cast<int>(coeffs.size());
        delay[head] = x;
        const int newest = head;
        head = (head + 1) % n;

        if (++counter < decim) return false;
        counter = 0;

        std::complex<double> acc{0.0, 0.0};
        int idx = newest;
        for (int k = 0; k < n; ++k) {
            acc += coeffs[k] * delay[idx];
            idx = (idx == 0) ? n - 1 : idx - 1;
        }
        out = acc;
        return true;
    }

    void reset() {
        std::fill(delay.begin(), delay.end(), std::complex<double>{0.0, 0.0});
        head = 0;
        counter = 0;
    }
};

// ---------------------------------------------------------------------------
// CarrierPll — 2nd-order (type II) carrier-tracking PLL for synchronous AM.
// GNU-Radio control_loop gains: with normalised loop bandwidth w = 2π·bw/fs,
//   denom = 1 + 2ζw + w²,  alpha = 4ζw/denom,  beta = 4w²/denom.
// process() returns the baseband-rotated sample in·conj(e^{jphase}).
// ---------------------------------------------------------------------------
struct CarrierPll {
    double phase     = 0.0;
    double freq      = 0.0;   // rad/sample
    double alpha     = 0.0;
    double beta      = 0.0;
    double freqLimit = 1.0;   // |freq| clamp, rad/sample

    void setLoopBandwidth(double bwHz, double fs, double zeta = 0.707) {
        const double w     = 2.0 * kPi * bwHz / fs;
        const double denom = 1.0 + 2.0 * zeta * w + w * w;
        alpha = (4.0 * zeta * w) / denom;
        beta  = (4.0 * w * w)   / denom;
    }

    void setFreqLimit(double maxRadPerSample) {
        freqLimit = std::abs(maxRadPerSample);
    }

    std::complex<double> process(std::complex<double> in) {
        const std::complex<double> ref(std::cos(phase), std::sin(phase));
        const std::complex<double> bb = in * std::conj(ref);
        const double err = std::atan2(bb.imag(), bb.real());

        freq += beta * err;
        if (freq >  freqLimit) freq =  freqLimit;
        if (freq < -freqLimit) freq = -freqLimit;

        phase += freq + alpha * err;
        if (phase >  kPi) phase -= 2.0 * kPi;
        if (phase < -kPi) phase += 2.0 * kPi;

        return bb;
    }

    void reset() {
        phase = 0.0;
        freq  = 0.0;
    }
};

} // namespace dsp
