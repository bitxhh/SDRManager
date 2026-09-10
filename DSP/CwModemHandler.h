#pragma once

#include "ModemHandler.h"

// ---------------------------------------------------------------------------
// CwModemHandler — Morse (CW) modem.
// Params: Bandwidth (100–1000 Hz filter width), Pitch (300–1000 Hz sidetone).
// ---------------------------------------------------------------------------
class CwModemHandler : public ModemHandler {
    Q_OBJECT

public:
    explicit CwModemHandler(double stationOffsetHz = 0.0,
                            double bandwidthHz     = 500.0,
                            double pitchHz         = 700.0,
                            QObject* parent        = nullptr);

    std::vector<modem::ParamDesc> paramDescriptors() const override;

protected:
    std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) override;

    void applyParam(ChannelModem& dem,
                    const QString& name, double value) override;

    const char* modemName() const override { return "CwModemHandler"; }
};
