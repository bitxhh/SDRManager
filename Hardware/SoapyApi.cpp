#include "SoapyApi.h"
#include "Logger.h"

#include <QLibrary>

namespace soapy {

namespace {

template <typename Fn>
bool resolveInto(QLibrary& lib, Fn& target, const char* name) {
    target = reinterpret_cast<Fn>(lib.resolve(name));
    return target != nullptr;
}

Api loadApi() {
    Api a;

    // QLibrary не выгружает DLL в деструкторе — статический объект не нужен.
    QLibrary lib(QStringLiteral("SoapySDR"));
    if (!lib.load()) {
        lib.setFileName(QStringLiteral("C:/Program Files/PothosSDR/bin/SoapySDR.dll"));
        if (!lib.load()) {
            LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Info,
                    "SoapyApi: SoapySDR.dll not found — SoapySDR devices disabled");
            return a;
        }
    }

    bool ok = true;
    ok &= resolveInto(lib, a.enumerateStrArgs, "SoapySDRDevice_enumerateStrArgs");
    ok &= resolveInto(lib, a.makeStrArgs,      "SoapySDRDevice_makeStrArgs");
    ok &= resolveInto(lib, a.unmake,           "SoapySDRDevice_unmake");
    ok &= resolveInto(lib, a.lastError,        "SoapySDRDevice_lastError");

    ok &= resolveInto(lib, a.setSampleRate,    "SoapySDRDevice_setSampleRate");
    ok &= resolveInto(lib, a.getSampleRate,    "SoapySDRDevice_getSampleRate");
    ok &= resolveInto(lib, a.listSampleRates,  "SoapySDRDevice_listSampleRates");
    ok &= resolveInto(lib, a.setFrequency,     "SoapySDRDevice_setFrequency");
    ok &= resolveInto(lib, a.getFrequency,     "SoapySDRDevice_getFrequency");
    ok &= resolveInto(lib, a.setGain,          "SoapySDRDevice_setGain");
    ok &= resolveInto(lib, a.getGain,          "SoapySDRDevice_getGain");
    ok &= resolveInto(lib, a.setGainMode,      "SoapySDRDevice_setGainMode");
    ok &= resolveInto(lib, a.getGainRange,     "SoapySDRDevice_getGainRange");

    ok &= resolveInto(lib, a.setupStream,      "SoapySDRDevice_setupStream");
    ok &= resolveInto(lib, a.closeStream,      "SoapySDRDevice_closeStream");
    ok &= resolveInto(lib, a.activateStream,   "SoapySDRDevice_activateStream");
    ok &= resolveInto(lib, a.deactivateStream, "SoapySDRDevice_deactivateStream");
    ok &= resolveInto(lib, a.readStream,       "SoapySDRDevice_readStream");

    ok &= resolveInto(lib, a.kwargsListClear,  "SoapySDRKwargsList_clear");
    ok &= resolveInto(lib, a.kwargsGet,        "SoapySDRKwargs_get");

    // SoapySDR_free появился не во всех сборках — необязателен
    // (без него массивы listSampleRates утекают, это единицы байт за сессию).
    resolveInto(lib, a.sdrFree, "SoapySDR_free");

    a.loaded = ok;
    LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Info,
            std::string("SoapyApi: SoapySDR.dll loaded, symbols ")
            + (ok ? "resolved" : "MISSING — SoapySDR devices disabled"));
    return a;
}

} // namespace

const Api& api() {
    static const Api instance = loadApi();
    return instance;
}

} // namespace soapy
