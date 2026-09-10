#pragma once

#include "ChannelModem.h"

// ---------------------------------------------------------------------------
// CwModem — Morse (CW) demodulator built on ChannelModem.
//
// After the base pipeline tunes the CW carrier to DC and hands us the complex
// IF stream, CwModem overrides produceAudio():
//
//   IF (complex) → FirComplexDecimator (narrow LPF, ± bandwidth/2 around DC)
//                → decimate to audioSR
//     BFO: mix up to an audible pitch (~700 Hz) with an NCO
//     audio = Re{ z · e^{jωt} }        // audible beat note
//
// A narrow bandwidth (default 500 Hz) gives the classic CW selectivity; the
// pitch sets the operator's preferred sidetone.
//
// demodulateIF() is unused (produceAudio is fully overridden).
// ---------------------------------------------------------------------------
class CwModem : public ChannelModem {
public:
    explicit CwModem(double inputSampleRateHz,
                     double stationOffsetHz,
                     double bandwidthHz = 500.0,
                     double pitchHz     = 700.0);

    void setBandwidth(double bandwidthHz);
    void setPitch(double pitchHz);

protected:
    // Never called (produceAudio overridden) — returns real part defensively.
    double demodulateIF(std::complex<double> ifSample, double ifPower) override;
    void produceAudio(std::complex<double> ifSample, double ifPower,
                      QVector<float>& out) override;
    void resetDemodState() override;
    const char* modemName() const override { return "CwModem"; }

private:
    void rebuildFilter();

    double pitchHz_;
    int    decim_;   // ifSR_ / audioSR_

    dsp::FirComplexDecimator chan_;   // narrow channel select + decimate
    dsp::Nco                 bfo_;    // beat-frequency oscillator (sidetone)
};
