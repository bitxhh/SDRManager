#include "SsbModemHandler.h"
#include "SsbModem.h"

SsbModemHandler::SsbModemHandler(double stationOffsetHz,
                                 int    sideband,
                                 double bandwidthHz,
                                 QObject* parent)
    : ModemHandler(stationOffsetHz, parent)
    , sideband_(sideband >= 0 ? +1 : -1)
{
    setParam(QStringLiteral("Bandwidth"), bandwidthHz);
}

std::vector<modem::ParamDesc> SsbModemHandler::paramDescriptors() const {
    return {
        modem::SpinParam{
            QStringLiteral("Bandwidth"),
            1, 4, 2.8,
            QStringLiteral(" kHz"), 0.1, 1000.0
        }
    };
}

std::unique_ptr<ChannelModem>
SsbModemHandler::createDemodulator(double sampleRateHz, double offsetHz,
                                   const std::map<QString, double>& params) {
    auto get = [&](const QString& k, double def) {
        auto it = params.find(k);
        return it != params.end() ? it->second : def;
    };
    const double bw = get(QStringLiteral("Bandwidth"), 2'800.0);
    return std::make_unique<SsbModem>(sampleRateHz, offsetHz, sideband_, bw,
        tapsParam(params, kFir1TapsKey, kDefaultFir1Taps),
        tapsParam(params, kChanTapsKey, kDefaultChanTaps));
}

void SsbModemHandler::applyParam(ChannelModem& dem,
                                 const QString& name, double value) {
    if (name == QLatin1String("Bandwidth"))
        static_cast<SsbModem&>(dem).setBandwidth(value);
}
