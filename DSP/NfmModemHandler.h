#pragma once

#include "ModemHandler.h"

// ---------------------------------------------------------------------------
// NfmModemHandler — narrowband FM modem.
// Params: Bandwidth (6–25 kHz channel), Deviation (2.5–8 kHz).
// ---------------------------------------------------------------------------
class NfmModemHandler : public ModemHandler {
    Q_OBJECT

public:
    explicit NfmModemHandler(double stationOffsetHz = 0.0,
                             double bandwidthHz     = 12'500.0,
                             double maxDeviationHz  = 5'000.0,
                             QObject* parent        = nullptr);

    std::vector<modem::ParamDesc> paramDescriptors() const override;

protected:
    std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) override;

    void applyParam(ChannelModem& dem,
                    const QString& name, double value) override;

    const char* modemName() const override { return "NfmModemHandler"; }
};
