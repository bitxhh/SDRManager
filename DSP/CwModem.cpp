#include "CwModem.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
// Narrow channel-select decimator taps (steep skirt for CW selectivity).
constexpr int kChanTaps = 255;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
CwModem::CwModem(double inputSampleRateHz,
                 double stationOffsetHz,
                 double bandwidthHz,
                 double pitchHz)
    : ChannelModem(inputSampleRateHz, stationOffsetHz,
                   100'000.0,         // FIR1 cutoff = fixed 100 kHz (wide anti-alias)
                   bandwidthHz,       // FIR2 cutoff (unused; produceAudio overridden)
                   20'000.0)          // min IF for CW
{
    bandwidth_ = std::clamp(bandwidthHz, 50.0, audioSR_ / 2.0 * 0.9);
    pitchHz_   = std::clamp(pitchHz, 300.0, 1'200.0);
    decim_     = std::max(1, static_cast<int>(std::round(ifSR_ / audioSR_)));

    rebuildFilter();
    bfo_.setFrequency(pitchHz_, audioSR_);

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "CwModem: inputSR=" + std::to_string(static_cast<int>(inputSR_))
            + " D1=" + std::to_string(D1_)
            + " IF=" + std::to_string(static_cast<int>(ifSR_)) + " Hz"
            + " decim=" + std::to_string(decim_)
            + " audio=" + std::to_string(static_cast<int>(audioSR_)) + " Hz"
            + " BW=" + std::to_string(static_cast<int>(bandwidth_)) + " Hz"
            + " pitch=" + std::to_string(static_cast<int>(pitchHz_)) + " Hz");
}

// ---------------------------------------------------------------------------
// rebuildFilter — narrow LPF (± bandwidth/2 around DC) + decimate to audioSR
// ---------------------------------------------------------------------------
void CwModem::rebuildFilter() {
    const double cutoffNorm = (bandwidth_ / 2.0) / ifSR_;
    chan_.setup(dsp::designLowpassFir(kChanTaps, cutoffNorm), decim_);
}

// ---------------------------------------------------------------------------
// setBandwidth — redesigns the narrow channel-select decimator
// ---------------------------------------------------------------------------
void CwModem::setBandwidth(double bandwidthHz) {
    bandwidth_ = std::clamp(bandwidthHz, 50.0, audioSR_ / 2.0 * 0.9);
    rebuildFilter();

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "CwModem: bandwidth set to "
            + std::to_string(static_cast<int>(bandwidth_)) + " Hz");
}

// ---------------------------------------------------------------------------
// setPitch — retunes the BFO sidetone (keeps phase to avoid clicks)
// ---------------------------------------------------------------------------
void CwModem::setPitch(double pitchHz) {
    pitchHz_ = std::clamp(pitchHz, 300.0, 1'200.0);
    bfo_.setFrequency(pitchHz_, audioSR_);
}

// ---------------------------------------------------------------------------
// demodulateIF — unused; produceAudio is fully overridden
// ---------------------------------------------------------------------------
double CwModem::demodulateIF(std::complex<double> ifSample, double /*ifPower*/) {
    return ifSample.real();
}

// ---------------------------------------------------------------------------
// produceAudio — narrow filter then BFO beat note
// ---------------------------------------------------------------------------
void CwModem::produceAudio(std::complex<double> ifSample, double /*ifPower*/,
                           QVector<float>& out) {
    std::complex<double> z;
    if (!chan_.process(ifSample, z))
        return;   // not a decimation output point yet

    const std::complex<double> tone = bfo_.mix(z);   // shift DC → pitch
    out.push_back(static_cast<float>(tone.real()));
}

// ---------------------------------------------------------------------------
// resetDemodState
// ---------------------------------------------------------------------------
void CwModem::resetDemodState() {
    chan_.reset();
    bfo_.reset();
}
