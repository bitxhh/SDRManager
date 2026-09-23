#include "FmModem.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <string>

static constexpr double kFmMaxDev = 75'000.0;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
FmModem::FmModem(double inputSampleRateHz,
                 double stationOffsetHz,
                 double deemphTauSec,
                 double bandwidthHz,
                 int    fir1Taps,
                 int    fir2Taps)
    : ChannelModem(inputSampleRateHz, stationOffsetHz,
                   bandwidthHz,       // FIR1 cutoff = user bandwidth
                   15'000.0,          // FIR2 cutoff = 15 kHz audio
                   400'000.0,         // min IF for WBFM
                   fir1Taps, fir2Taps)
    , deemphTau_(deemphTauSec)
{
    bandwidth_ = std::clamp(bandwidthHz, 30'000.0, ifSR_ / 2.0 * 0.9);
    // Базовый конструктор получил сырой bandwidthHz (ifSR_ до него неизвестен):
    // без пересборки FIR1 при bw > IF/2 соседние станции заворачиваются в IF.
    redesignFir1(bandwidth_);

    demodGain_ = ifSR_ / (2.0 * dsp::kPi * kFmMaxDev);
    deemphP_   = std::exp(-1.0 / (deemphTau_ * ifSR_));

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "FmModem: inputSR=" + std::to_string(static_cast<int>(inputSR_))
            + " D1=" + std::to_string(D1_)
            + " IF=" + std::to_string(static_cast<int>(ifSR_)) + " Hz"
            + " audio=" + std::to_string(static_cast<int>(audioSR_)) + " Hz"
            + " BW=" + std::to_string(static_cast<int>(bandwidth_)) + " Hz"
            + " demodGain=" + std::to_string(demodGain_)
            + " deemph t=" + std::to_string(static_cast<int>(deemphTau_ * 1e6)) + " us");
}

// ---------------------------------------------------------------------------
// setBandwidth — redesigns FIR1 (pre-decimation channel width)
// ---------------------------------------------------------------------------
void FmModem::setBandwidth(double bandwidthHz) {
    bandwidth_ = std::clamp(bandwidthHz, 30'000.0, ifSR_ / 2.0 * 0.9);
    redesignFir1(bandwidth_);

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "FmModem: bandwidth set to "
            + std::to_string(static_cast<int>(bandwidth_)) + " Hz");
}

// ---------------------------------------------------------------------------
// demodulateIF — FM discriminator + de-emphasis
// ---------------------------------------------------------------------------
double FmModem::demodulateIF(std::complex<double> ifSample, double /*ifPower*/) {
    // FM discriminator: phase difference between consecutive IF samples
    const std::complex<double> prod = ifSample * std::conj(prevIF_);
    prevIF_ = ifSample;
    const double demod = std::atan2(prod.imag(), prod.real()) * demodGain_;

    // De-emphasis: first-order IIR lowpass (τ = 50/75 µs)
    deemphState_ = (1.0 - deemphP_) * demod + deemphP_ * deemphState_;

    return deemphState_;
}

// ---------------------------------------------------------------------------
// resetDemodState — called by base setOffset()
// ---------------------------------------------------------------------------
void FmModem::resetDemodState() {
    prevIF_      = {1.0, 0.0};
    deemphState_ = 0.0;
}
