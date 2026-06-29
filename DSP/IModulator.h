#pragma once

#include <QVector>

// ---------------------------------------------------------------------------
// IModulator — TX engine of a modem. Mirror of the demodulator: audio → IQ.
//
// Not yet used: no modem returns one (IModem::makeModulator() == nullptr).
// Reserved for the future TX pipeline.
// ---------------------------------------------------------------------------
class IModulator {
public:
    virtual ~IModulator() = default;

    // Mono audio [-1,1] → interleaved IQ at the modem's output (IF/input) Fs.
    virtual QVector<float> modulate(const float* audio, int count) = 0;

    virtual double audioSampleRate()  const = 0;  // expected input audio Fs
    virtual double outputSampleRate() const = 0;  // output IQ Fs
};
