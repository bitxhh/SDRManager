#include "NfmModemHandler.h"
#include "NfmModem.h"

NfmModemHandler::NfmModemHandler(double stationOffsetHz,
                                 double bandwidthHz,
                                 double maxDeviationHz,
                                 QObject* parent)
    : ModemHandler(stationOffsetHz, parent)
{
    setParam(QStringLiteral("Bandwidth"), bandwidthHz);
    setParam(QStringLiteral("Deviation"), maxDeviationHz);
}

std::vector<modem::ParamDesc> NfmModemHandler::paramDescriptors() const {
    return {
        modem::SpinParam{
            QStringLiteral("Bandwidth"),
            6, 25, 12.5,
            QStringLiteral(" kHz"), 0.5, 1000.0
        },
        modem::SpinParam{
            QStringLiteral("Deviation"),
            2.5, 8, 5,
            QStringLiteral(" kHz"), 0.5, 1000.0
        }
    };
}

std::unique_ptr<ChannelModem>
NfmModemHandler::createDemodulator(double sampleRateHz, double offsetHz,
                                   const std::map<QString, double>& params) {
    auto get = [&](const QString& k, double def) {
        auto it = params.find(k);
        return it != params.end() ? it->second : def;
    };
    const double bw  = get(QStringLiteral("Bandwidth"), 12'500.0);
    const double dev = get(QStringLiteral("Deviation"),  5'000.0);
    return std::make_unique<NfmModem>(sampleRateHz, offsetHz, bw, dev);
}

void NfmModemHandler::applyParam(ChannelModem& dem,
                                 const QString& name, double value) {
    if (name == QLatin1String("Bandwidth"))
        static_cast<NfmModem&>(dem).setBandwidth(value);
    else if (name == QLatin1String("Deviation"))
        static_cast<NfmModem&>(dem).setDeviation(value);
}
