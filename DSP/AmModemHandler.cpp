#include "AmModemHandler.h"
#include "AmModem.h"

AmModemHandler::AmModemHandler(double stationOffsetHz,
                               double bandwidthHz,
                               QObject* parent)
    : ModemHandler(stationOffsetHz, parent)
{
    setParam(QStringLiteral("Bandwidth"), bandwidthHz);
}

std::vector<modem::ParamDesc> AmModemHandler::paramDescriptors() const {
    return {
        modem::SpinParam{
            QStringLiteral("Bandwidth"),
            1, 20, 5,
            QStringLiteral(" kHz"), 1, 1000.0
        }
    };
}

std::unique_ptr<ChannelModem>
AmModemHandler::createDemodulator(double sampleRateHz, double offsetHz,
                                  const std::map<QString, double>& params) {
    auto it = params.find(QStringLiteral("Bandwidth"));
    const double bw = (it != params.end()) ? it->second : 5'000.0;
    return std::make_unique<AmModem>(sampleRateHz, offsetHz, bw);
}

void AmModemHandler::applyParam(ChannelModem& dem,
                                const QString& name, double value) {
    if (name == QLatin1String("Bandwidth"))
        static_cast<AmModem&>(dem).setBandwidth(value);
}
