#pragma once

#include "ModemHandler.h"

class CtcssDetector;
class DcsDetector;

// ---------------------------------------------------------------------------
// NfmModemHandler — narrowband FM modem.
// Params: Bandwidth (6–25 kHz channel), Deviation (2.5–8 kHz),
//         CTCSS (tone squelch target, 0 = off; see CtcssDetector),
//         DCS (digital code squelch target, 0 = off; see DcsDetector).
// The audio chain always runs a DcsDetector and then a CtcssDetector: they
// report the received DCS code (dcsCodeChanged) and sub-audible tone
// (ctcssToneChanged); the latter also highpasses the audio at 300 Hz.
// ---------------------------------------------------------------------------
class NfmModemHandler : public ModemHandler {
    Q_OBJECT

public:
    explicit NfmModemHandler(double stationOffsetHz = 0.0,
                             double bandwidthHz     = 12'500.0,
                             double maxDeviationHz  = 5'000.0,
                             QObject* parent        = nullptr);

    std::vector<modem::ParamDesc> paramDescriptors() const override;

    // "CTCSS" combo: Off + the 50 standard tones.
    static modem::ComboParam ctcssParam();
    // "DCS" combo: Off + the 104 standard codes (value = octal code number).
    static modem::ComboParam dcsParam();

    void processBlock(const float* iq, int count, double sampleRateHz) override;
    void onStreamStopped() override;

signals:
    // Emitted (worker thread) when the detected CTCSS tone changes; 0 = none.
    void ctcssToneChanged(double hz);
    // Emitted (worker thread) when the detected DCS code changes; 0 = none.
    void dcsCodeChanged(int code);

protected:
    std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) override;

    void applyParam(ChannelModem& dem,
                    const QString& name, double value) override;

    void buildAudioChain(ChannelModem& dem,
                         const std::map<QString, double>& params) override;

    const char* modemName() const override { return "NfmModemHandler"; }

private:
    CtcssDetector* ctcss_{nullptr};   // owned by dem_'s audio chain
    double         lastTone_{0.0};
    DcsDetector*   dcs_{nullptr};     // owned by dem_'s audio chain
    int            lastDcs_{0};
};
