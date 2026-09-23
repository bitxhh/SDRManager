#pragma once

#include "ChannelModem.h"

// ---------------------------------------------------------------------------
// FmModem — WBFM demodulator built on ChannelModem.
//
// FM-specific stages (demodulateIF):
//   FM discriminator (atan2 of conjugate product)  →  de-emphasis IIR
//
// setBandwidth() redesigns FIR1 (pre-decimation channel filter).
// ---------------------------------------------------------------------------
class FmModem : public ChannelModem {
public:
    explicit FmModem(double inputSampleRateHz,
                     double stationOffsetHz,
                     double deemphTauSec    = 50e-6,
                     double bandwidthHz     = 100'000.0,
                     int    fir1Taps        = kDefaultFir1Taps,
                     int    fir2Taps        = kDefaultFir2Taps);

    void setBandwidth(double bandwidthHz);

protected:
    double demodulateIF(std::complex<double> ifSample, double ifPower) override;
    void resetDemodState() override;
    const char* modemName() const override { return "FmModem"; }

private:
    double deemphTau_;

    // FM discriminator state
    std::complex<double> prevIF_{1.0, 0.0};
    double               demodGain_;

    // De-emphasis IIR
    double deemphP_{0.0};
    double deemphState_{0.0};
};
