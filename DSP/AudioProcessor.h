#pragma once

#include <QString>

// ---------------------------------------------------------------------------
// IAudioProcessor — post-demodulation audio stage (after FIR2 / ÷D2).
//
// ChannelModem runs its processors in insertion order, in place, on every
// audio block returned by pushBlock(). Base for NR / notch / AGC stages.
//
//   prepare()  — called once when added to a modem, with the audio rate;
//   process()  — in-place, n samples, same thread as pushBlock();
//   reset()    — drop history (station change: ChannelModem::setOffset);
//   setParam() — named live parameter; return true if the name is ours.
//                ModemHandler routes every setParam() through the chain
//                first, so audio params need no per-modem applyParam().
// ---------------------------------------------------------------------------
class IAudioProcessor {
public:
    virtual ~IAudioProcessor() = default;

    virtual void prepare(double /*audioSampleRateHz*/) {}
    virtual void process(float* samples, int n) = 0;
    virtual void reset() {}
    virtual bool setParam(const QString& /*name*/, double /*value*/) { return false; }
};
