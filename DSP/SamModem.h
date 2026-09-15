#pragma once

#include "ChannelModem.h"

// ---------------------------------------------------------------------------
// SamModem — synchronous AM (SAM / DSB) demodulator built on ChannelModem.
//
// Unlike the envelope detector (AmModem), SAM phase-locks a carrier PLL to the
// residual carrier and takes the real part of the derotated baseband. This is
// immune to selective-fading distortion and works on suppressed/reduced-carrier
// double-sideband signals.
//
// SAM-specific stages (demodulateIF):
//   CarrierPll derotate  →  Re{bb}  →  DC removal (IIR HP ~20 Hz)
//
// Params:
//   Bandwidth — audio bandwidth (FIR2 cutoff).
//   PLL BW    — carrier-tracking loop bandwidth (pull-in speed vs. noise).
// ---------------------------------------------------------------------------
class SamModem : public ChannelModem {
public:
    explicit SamModem(double inputSampleRateHz,
                      double stationOffsetHz,
                      double bandwidthHz  = 5'000.0,
                      double pllBwHz      = 100.0,
                      int    fir1Taps     = kDefaultFir1Taps,
                      int    fir2Taps     = kDefaultFir2Taps);

    void setBandwidth(double bandwidthHz);
    void setPllBandwidth(double pllBwHz);

protected:
    double demodulateIF(std::complex<double> ifSample, double ifPower) override;
    void resetDemodState() override;
    const char* modemName() const override { return "SamModem"; }

private:
    dsp::CarrierPll   pll_;      // locks the residual carrier to DC
    dsp::IirHighpass1 audioDc_;  // removes the carrier DC term after detection
    double            pllBw_;
};
