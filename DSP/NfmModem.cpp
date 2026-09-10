#include "NfmModem.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
NfmModem::NfmModem(double inputSampleRateHz,
                   double stationOffsetHz,
                   double bandwidthHz,
                   double maxDeviationHz)
    : ChannelModem(inputSampleRateHz, stationOffsetHz,
                   bandwidthHz / 2.0,   // FIR1 cutoff = half the channel width
                   4'000.0,             // FIR2 cutoff = 4 kHz voice audio
                   100'000.0)           // min IF for NFM
    , maxDeviation_(maxDeviationHz)
{
    bandwidth_ = std::clamp(bandwidthHz, 6'000.0, ifSR_ * 0.9);

    demodGain_ = ifSR_ / (2.0 * dsp::kPi * maxDeviation_);

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "NfmModem: inputSR=" + std::to_string(static_cast<int>(inputSR_))
            + " D1=" + std::to_string(D1_)
            + " IF=" + std::to_string(static_cast<int>(ifSR_)) + " Hz"
            + " audio=" + std::to_string(static_cast<int>(audioSR_)) + " Hz"
            + " BW=" + std::to_string(static_cast<int>(bandwidth_)) + " Hz"
            + " dev=" + std::to_string(static_cast<int>(maxDeviation_)) + " Hz"
            + " demodGain=" + std::to_string(demodGain_));
}

// ---------------------------------------------------------------------------
// setBandwidth — full channel width; FIR1 cutoff = bandwidth/2
// ---------------------------------------------------------------------------
void NfmModem::setBandwidth(double bandwidthHz) {
    bandwidth_ = std::clamp(bandwidthHz, 6'000.0, ifSR_ * 0.9);
    redesignFir1(bandwidth_ / 2.0);

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            "NfmModem: bandwidth set to "
            + std::to_string(static_cast<int>(bandwidth_)) + " Hz");
}

// ---------------------------------------------------------------------------
// setDeviation — peak deviation for full-scale audio
// ---------------------------------------------------------------------------
void NfmModem::setDeviation(double maxDeviationHz) {
    maxDeviation_ = std::clamp(maxDeviationHz, 1'000.0, 15'000.0);
    demodGain_    = ifSR_ / (2.0 * dsp::kPi * maxDeviation_);
}

// ---------------------------------------------------------------------------
// demodulateIF — FM discriminator (no de-emphasis)
// ---------------------------------------------------------------------------
double NfmModem::demodulateIF(std::complex<double> ifSample, double /*ifPower*/) {
    const std::complex<double> prod = ifSample * std::conj(prevIF_);
    prevIF_ = ifSample;
    return std::atan2(prod.imag(), prod.real()) * demodGain_;
}

// ---------------------------------------------------------------------------
// resetDemodState
// ---------------------------------------------------------------------------
void NfmModem::resetDemodState() {
    prevIF_ = {1.0, 0.0};
}
