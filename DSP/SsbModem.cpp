#include "SsbModem.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
// Hilbert transformer taps (odd → Type III integer group delay).
constexpr int kHilbertTaps = 127;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SsbModem::SsbModem(double inputSampleRateHz,
                   double stationOffsetHz,
                   int    sideband,
                   double bandwidthHz,
                   int    fir1Taps,
                   int    chanTaps)
    : ChannelModem(inputSampleRateHz, stationOffsetHz,
                   100'000.0,         // FIR1 cutoff = fixed 100 kHz (wide anti-alias)
                   bandwidthHz,       // FIR2 cutoff (unused; produceAudio overridden)
                   20'000.0,          // min IF for SSB
                   fir1Taps)
    , sideband_(sideband >= 0 ? +1 : -1)
    , name_(sideband >= 0 ? "UsbModem" : "LsbModem")
    , chanTaps_(chanTaps)
{
    bandwidth_ = std::clamp(bandwidthHz, 1'000.0, audioSR_ / 2.0 * 0.9);
    decim_     = std::max(1, static_cast<int>(std::round(ifSR_ / audioSR_)));

    // Hilbert on Q + matched integer delay on I (group delay = (N-1)/2).
    hilbert_.setCoeffs(dsp::designHilbertFir(kHilbertTaps));
    delayI_.setDelay((kHilbertTaps - 1) / 2);

    rebuildDecimator();

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            std::string(name_) + ": inputSR=" + std::to_string(static_cast<int>(inputSR_))
            + " D1=" + std::to_string(D1_)
            + " IF=" + std::to_string(static_cast<int>(ifSR_)) + " Hz"
            + " decim=" + std::to_string(decim_)
            + " audio=" + std::to_string(static_cast<int>(audioSR_)) + " Hz"
            + " BW=" + std::to_string(static_cast<int>(bandwidth_)) + " Hz"
            + " sideband=" + (sideband_ > 0 ? std::string("USB") : std::string("LSB"))
            + " taps FIR1=" + std::to_string(fir1Taps) + " chan=" + std::to_string(chanTaps_));
}

// ---------------------------------------------------------------------------
// rebuildDecimator — channel LPF (± bandwidth around DC) + decimate to audioSR
// ---------------------------------------------------------------------------
void SsbModem::rebuildDecimator() {
    const double cutoffNorm = bandwidth_ / ifSR_;
    chan_.setup(dsp::designLowpassFir(chanTaps_, cutoffNorm), decim_);
}

// ---------------------------------------------------------------------------
// setBandwidth — redesigns the channel-select decimator
// ---------------------------------------------------------------------------
void SsbModem::setBandwidth(double bandwidthHz) {
    bandwidth_ = std::clamp(bandwidthHz, 1'000.0, audioSR_ / 2.0 * 0.9);
    rebuildDecimator();

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            std::string(name_) + ": bandwidth set to "
            + std::to_string(static_cast<int>(bandwidth_)) + " Hz");
}

// ---------------------------------------------------------------------------
// demodulateIF — unused; produceAudio is fully overridden
// ---------------------------------------------------------------------------
double SsbModem::demodulateIF(std::complex<double> ifSample, double /*ifPower*/) {
    return ifSample.real();
}

// ---------------------------------------------------------------------------
// produceAudio — phasing-method SSB: decimate → Hilbert{Q} vs delayed I
// ---------------------------------------------------------------------------
void SsbModem::produceAudio(std::complex<double> ifSample, double /*ifPower*/,
                            QVector<float>& out) {
    std::complex<double> z;
    if (!chan_.process(ifSample, z))
        return;   // not a decimation output point yet

    const double id = delayI_.process(z.real());   // delayed in-phase
    const double hq = hilbert_.process(z.imag());  // Hilbert-shifted quadrature

    // USB: id − hq   LSB: id + hq   (0.5 undoes the phasing 2× gain)
    const double audio = 0.5 * (id - static_cast<double>(sideband_) * hq);
    out.push_back(static_cast<float>(audio));
}

// ---------------------------------------------------------------------------
// resetDemodState
// ---------------------------------------------------------------------------
void SsbModem::resetDemodState() {
    chan_.reset();
    hilbert_.reset();
    delayI_.reset();
}
