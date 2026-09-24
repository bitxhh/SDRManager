#pragma once

#include <QMetaType>
#include <QVector>
#include <memory>

struct FftFrame {
    QVector<double> freqMHz;
    QVector<double> powerDb;   // dBFS per bin, coherent-normalized (tone reads its power)

    double binHz    = 0.0;     // bin spacing, Hz
    double enbwBins = 1.0;     // window equivalent noise bandwidth, in bins (Hann ≈ 1.5)

    double enbwHz() const { return enbwBins * binHz; }
};
Q_DECLARE_METATYPE(FftFrame)

// ---------------------------------------------------------------------------
// FftProcessor — stateless public API, stateful plan cache underneath.
//
// FFTW plan creation is expensive (~ms). Plans are cached by fftSize in a
// thread_local map so the same worker thread always reuses its plan.
// Calling process() from multiple threads is safe — each thread gets its own
// plan instance.
// ---------------------------------------------------------------------------
class FftProcessor {
public:
    // Process one block of interleaved float32 I/Q samples (normalized to [-1, 1]).
    // Returns a frame ready to hand to QCustomPlot::setData().
    //
    // iq           — interleaved float32 [I0,Q0,I1,Q1,...], count I/Q pairs
    // centerFreqMHz — value from the spin-box (e.g. 102.0)
    // sampleRateHz  — value from Device::get_sample_rate() (e.g. 2 000 000)
    static FftFrame process(const float* iq, int count,
                            double centerFreqMHz,
                            double sampleRateHz);

    // Total power (dBFS) of bins [first, last] inclusive: linear sum of bin
    // powers divided by enbwBins. A tone whose main lobe fits in the range
    // reads its true power; so does noise (power in that bandwidth).
    // Divide by frame.enbwHz() instead of enbwBins for a dBFS/Hz density.
    static double bandPowerDb(const FftFrame& frame, int first, int last);

    // Noise floor estimate (dB/bin) over bins [first, last] inclusive: the
    // given percentile of bin levels (0.5 = median). A low percentile (0.2)
    // ignores narrowband signals occupying up to ~80% of the range.
    // Works on any trace (Live, Average, …). NaN if the range is empty.
    static double noiseFloorDb(const QVector<double>& powerDb, int first, int last,
                               double percentile = 0.2);
};
