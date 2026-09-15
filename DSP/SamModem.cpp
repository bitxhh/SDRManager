#include "SamModem.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
// Carrier pull-in range (Hz). The PLL frequency term is clamped to ±this.
constexpr double kPullRangeHz = 1'000.0;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SamModem::SamModem(double inputSampleRateHz,
                   double stationOffsetHz,
                   double bandwidthHz,
                   double pllBwHz,
                   int    fir1Taps,
                   int    fir2Taps)
    : ChannelModem(inputSampleRateHz, stationOffsetHz,
                   100'000.0,         // FIR1 cutoff = fixed 100 kHz (wide anti-alias)
                   bandwidthHz,       // FIR2 cutoff = user bandwidth
                   20'000.0,          // min IF for SAM
                   fir1Taps, fir2Taps)
    , pllBw_(pllBwHz)
{
    bandwidth_ = std::clamp(bandwidthHz, 1'000.0, audioSR_ / 2.0 * 0.9);

    // Carrier-tracking PLL: loop bandwidth + pull-in range (rad/sample).
    pll_.setLoopBandwidth(pllBw_, ifSR_);
    pll_.setFreqLimit(2.0 * dsp::kPi * kPullRangeHz / ifSR_);

    // Post-detection DC removal: IIR highpass at ~20 Hz.
    audioDc_.setCutoff(20.0, ifSR_);

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "SamModem: inputSR=" + std::to_string(static_cast<int>(inputSR_))
            + " D1=" + std::to_string(D1_)
            + " IF=" + std::to_string(static_cast<int>(ifSR_)) + " Hz"
            + " audio=" + std::to_string(static_cast<int>(audioSR_)) + " Hz"
            + " BW=" + std::to_string(static_cast<int>(bandwidth_)) + " Hz"
            + " pllBW=" + std::to_string(static_cast<int>(pllBw_)) + " Hz");
}

// ---------------------------------------------------------------------------
// setBandwidth — redesigns FIR2 (audio bandwidth)
// ---------------------------------------------------------------------------
void SamModem::setBandwidth(double bandwidthHz) {
    bandwidth_ = std::clamp(bandwidthHz, 1'000.0, audioSR_ / 2.0 * 0.9);
    redesignFir2(bandwidth_);

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "SamModem: bandwidth set to "
            + std::to_string(static_cast<int>(bandwidth_)) + " Hz");
}

// ---------------------------------------------------------------------------
// setPllBandwidth — carrier-tracking loop bandwidth
// ---------------------------------------------------------------------------
void SamModem::setPllBandwidth(double pllBwHz) {
    pllBw_ = std::clamp(pllBwHz, 10.0, 500.0);
    pll_.setLoopBandwidth(pllBw_, ifSR_);
}

// ---------------------------------------------------------------------------
// demodulateIF — synchronous detection: derotate carrier, take real part
// ---------------------------------------------------------------------------
double SamModem::demodulateIF(std::complex<double> ifSample, double /*ifPower*/) {
    const std::complex<double> bb = pll_.process(ifSample);
    return audioDc_.process(bb.real());
}

// ---------------------------------------------------------------------------
// resetDemodState — called by base setOffset()
// ---------------------------------------------------------------------------
void SamModem::resetDemodState() {
    pll_.reset();
    audioDc_.reset();
}
