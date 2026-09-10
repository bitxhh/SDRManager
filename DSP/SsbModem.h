#pragma once

#include "ChannelModem.h"

// ---------------------------------------------------------------------------
// SsbModem — single-sideband (USB/LSB) demodulator built on ChannelModem.
//
// Phasing method. After the base pipeline tunes the suppressed carrier to DC
// and hands us the complex IF stream, SsbModem overrides produceAudio():
//
//   IF (complex) → FirComplexDecimator (LPF = audio BW) → decimate to audioSR
//     I = Re{z}, Q = Im{z}
//     id = DelayLine(I)      // matches Hilbert group delay
//     hq = Hilbert{Q}
//     audio = 0.5 * (id − sideband · hq)
//
//   sideband = +1 → USB (rejects LSB),  −1 → LSB (rejects USB).
//
// demodulateIF() is unused (produceAudio is fully overridden).
// ---------------------------------------------------------------------------
class SsbModem : public ChannelModem {
public:
    // sideband: +1 = USB, -1 = LSB.
    explicit SsbModem(double inputSampleRateHz,
                      double stationOffsetHz,
                      int    sideband,
                      double bandwidthHz = 2'800.0);

    void setBandwidth(double bandwidthHz);

protected:
    // Never called (produceAudio overridden) — returns real part defensively.
    double demodulateIF(std::complex<double> ifSample, double ifPower) override;
    void produceAudio(std::complex<double> ifSample, double ifPower,
                      QVector<float>& out) override;
    void resetDemodState() override;
    const char* modemName() const override { return name_; }

private:
    void rebuildDecimator();

    int         sideband_;         // +1 USB, -1 LSB
    int         decim_;            // ifSR_ / audioSR_
    const char* name_;

    dsp::FirComplexDecimator chan_;   // channel select + decimate to audio rate
    dsp::FirReal             hilbert_; // 90° phase shift on Q
    dsp::DelayLine           delayI_;  // group-delay match on I
};
