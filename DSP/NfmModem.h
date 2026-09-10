#pragma once

#include "ChannelModem.h"

// ---------------------------------------------------------------------------
// NfmModem — narrowband FM demodulator built on ChannelModem.
//
// Same discriminator core as FmModem, but:
//   • narrow channel filter (FIR1 cutoff = bandwidth/2, e.g. 12.5 kHz channel)
//   • narrow audio filter   (FIR2 cutoff ≈ 4 kHz voice)
//   • deviation-based gain   (±5 kHz full scale, not ±75 kHz)
//   • no de-emphasis         (comms channels are flat)
//
// Used for PMR446 / marine / 2 m / 70 cm voice.
// ---------------------------------------------------------------------------
class NfmModem : public ChannelModem {
public:
    explicit NfmModem(double inputSampleRateHz,
                      double stationOffsetHz,
                      double bandwidthHz    = 12'500.0,
                      double maxDeviationHz = 5'000.0);

    // Full channel bandwidth (Hz). FIR1 cutoff = bandwidth/2.
    void setBandwidth(double bandwidthHz);
    // Peak deviation used to normalise the discriminator output.
    void setDeviation(double maxDeviationHz);

protected:
    double demodulateIF(std::complex<double> ifSample, double ifPower) override;
    void resetDemodState() override;
    const char* modemName() const override { return "NfmModem"; }

private:
    double maxDeviation_;

    // FM discriminator state
    std::complex<double> prevIF_{1.0, 0.0};
    double               demodGain_;
};
