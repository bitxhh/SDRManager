#pragma once

#include "ChannelModem.h"

// ---------------------------------------------------------------------------
// AmModem — AM envelope demodulator built on ChannelModem.
//
// AM-specific stages (demodulateIF):
//   Envelope detection: sqrt(I² + Q²)  →  DC removal (IIR HP ~20 Hz)
//
// setBandwidth() redesigns FIR2 (audio bandwidth filter).
// ---------------------------------------------------------------------------
class AmModem : public ChannelModem {
public:
    explicit AmModem(double inputSampleRateHz,
                     double stationOffsetHz,
                     double bandwidthHz = 5'000.0);

    void setBandwidth(double bandwidthHz);

protected:
    double demodulateIF(std::complex<double> ifSample, double ifPower) override;
    void resetDemodState() override;
    const char* modemName() const override { return "AmModem"; }

private:
    dsp::IirHighpass1 envDc_;   // envelope DC removal (~20 Hz)
};
