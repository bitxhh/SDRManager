#include "SoapyDevice.h"
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace {
// Типовые rate'ы RTL-SDR-класса железа — используются до init(), когда
// listSampleRates ещё недоступен (комбобокс в UI строится до открытия).
const QList<double> kFallbackRates = {
    0.25e6, 1.024e6, 1.6e6, 2.0e6, 2.048e6, 2.4e6, 2.56e6, 3.2e6
};
} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
SoapyDevice::SoapyDevice(const QString& makeArgs, const QString& id,
                         const QString& name, QObject* parent)
    : IDevice(parent)
    , makeArgs_(makeArgs)
    , id_(id)
    , name_(name)
{
}

SoapyDevice::~SoapyDevice() {
    try {
        close();
    } catch (...) {
        // деструктор не должен бросать
    }
}

double SoapyDevice::maxSampleRate() const {
    const bool rtl = makeArgs_.contains(QStringLiteral("driver=rtlsdr"))
                  || id_.startsWith(QStringLiteral("soapy:rtlsdr"));
    return rtl ? 2.4e6 : std::numeric_limits<double>::infinity();
}

// hz > предела → наибольший разрешённый rate из списка (или сам предел).
double SoapyDevice::clampRate(double hz, const QList<double>& allowed) const {
    const double maxHz = maxSampleRate();
    if (hz <= maxHz) return hz;
    return allowed.isEmpty() ? maxHz : allowed.last();
}

QString SoapyDevice::soapyError() const {
    const auto& a = soapy::api();
    const char* msg = a.lastError ? a.lastError() : nullptr;
    return msg && *msg ? QString::fromUtf8(msg) : QStringLiteral("unknown error");
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void SoapyDevice::init(const QList<ChannelDescriptor>& /*channels*/) {
    const auto& a = soapy::api();
    if (!a.loaded)
        throw std::runtime_error("SoapyDevice: SoapySDR.dll is not available");

    std::lock_guard lock(apiMutex_);

    if (!dev_) {
        dev_ = a.makeStrArgs(makeArgs_.toUtf8().constData());
        if (!dev_)
            throw std::runtime_error("SoapyDevice: failed to open device: "
                                     + soapyError().toStdString());
    }

    // Ручное управление усилением (единый слайдер в UI).
    a.setGainMode(dev_, soapy::kRx, 0, false);

    const SoapySDRRange range = a.getGainRange(dev_, soapy::kRx, 0);
    if (range.maximum > range.minimum) {
        gainMin_ = range.minimum;
        gainMax_ = range.maximum;
    }

    rates_.clear();
    size_t  n     = 0;
    double* rates = a.listSampleRates(dev_, soapy::kRx, 0, &n);
    if (rates) {
        const double maxHz = maxSampleRate();
        for (size_t i = 0; i < n; ++i)
            if (rates[i] <= maxHz) rates_.append(rates[i]);
        std::sort(rates_.begin(), rates_.end());
        if (a.sdrFree) a.sdrFree(rates);   // без SoapySDR_free — утечка в пару сотен байт
    }

    sampleRateHz_ = clampRate(sampleRateHz_, rates_.isEmpty() ? fallbackRates() : rates_);
    if (a.setSampleRate(dev_, soapy::kRx, 0, sampleRateHz_) != 0)
        throw std::runtime_error("SoapyDevice: setSampleRate failed: "
                                 + soapyError().toStdString());
    if (a.setFrequency(dev_, soapy::kRx, 0, frequencyHz_, nullptr) != 0)
        throw std::runtime_error("SoapyDevice: setFrequency failed: "
                                 + soapyError().toStdString());
    a.setGain(dev_, soapy::kRx, 0, gainDb_);

    LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Info,
            "SoapyDevice: initialized " + id_.toStdString()
            + " (gain range " + std::to_string(gainMin_) + ".."
            + std::to_string(gainMax_) + " dB)");

    setState(DeviceState::Ready);
}

void SoapyDevice::close() {
    const auto& a = soapy::api();

    if (state_ == DeviceState::Streaming)
        stopStream();

    std::lock_guard lock(apiMutex_);
    if (dev_) {
        a.unmake(dev_);
        dev_ = nullptr;
    }
    setState(DeviceState::Connected);
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
void SoapyDevice::setSampleRate(double hz) {
    const auto& a = soapy::api();
    std::lock_guard lock(apiMutex_);

    hz = clampRate(hz, rates_.isEmpty() ? fallbackRates() : rates_);
    if (dev_ && a.setSampleRate(dev_, soapy::kRx, 0, hz) != 0)
        throw std::runtime_error("SoapyDevice: setSampleRate failed: "
                                 + soapyError().toStdString());
    sampleRateHz_ = dev_ ? a.getSampleRate(dev_, soapy::kRx, 0) : hz;
    emit sampleRateChanged(sampleRateHz_);
}

double SoapyDevice::sampleRate() const { return sampleRateHz_; }

QList<double> SoapyDevice::supportedSampleRates() const {
    std::lock_guard lock(apiMutex_);
    return rates_.isEmpty() ? fallbackRates() : rates_;
}

QList<double> SoapyDevice::fallbackRates() const {
    const double maxHz = maxSampleRate();
    QList<double> out;
    for (double r : kFallbackRates)
        if (r <= maxHz) out.append(r);
    return out;
}

void SoapyDevice::setFrequency(double hz) {
    const auto& a = soapy::api();
    std::lock_guard lock(apiMutex_);

    // На живом стриме тюнинг (USB control transfer + I2C к тюнеру, десятки мс)
    // откладывается в поток воркера: UI не блокируется, а серия шагов колеса
    // схлопывается в один реальный тюнинг на блок.
    if (stream_) {
        pendingFrequency_ = hz;
        frequencyHz_      = hz;
        return;
    }

    if (dev_ && a.setFrequency(dev_, soapy::kRx, 0, hz, nullptr) != 0)
        throw std::runtime_error("SoapyDevice: setFrequency failed: "
                                 + soapyError().toStdString());
    frequencyHz_ = hz;
    // Намеренно без emit retuned(): нет park-handshake — см. заголовок.
}

double SoapyDevice::frequency() const { return frequencyHz_; }

void SoapyDevice::setGain(double dB) {
    const auto& a = soapy::api();
    std::lock_guard lock(apiMutex_);

    const double clamped = std::clamp(dB, gainMin_, gainMax_);
    if (dev_)
        a.setGain(dev_, soapy::kRx, 0, clamped);
    gainDb_ = clamped;
}

double SoapyDevice::gain() const { return gainDb_; }

double SoapyDevice::maxGain() const {
    std::lock_guard lock(apiMutex_);
    return gainMax_;
}

// ---------------------------------------------------------------------------
// Streaming (вызывается из потока RxWorker)
// ---------------------------------------------------------------------------
void SoapyDevice::startStream() {
    const auto& a = soapy::api();
    std::lock_guard lock(apiMutex_);

    if (!dev_)
        throw std::runtime_error("SoapyDevice: startStream before init()");
    if (stream_)
        return;   // уже запущен

    const size_t chans[1] = {0};
    // Дефолт SoapyRTLSDR — 15 буферов (~0.6 с при 3.2 MS/s); при кратких
    // задержках планировщика Windows этого мало, и драйвер выкидывает блоки
    // (щелчки в звуке). Лишние ключи другие драйверы игнорируют.
    char kBuffersKey[] = "buffers";
    char kBuffersVal[] = "64";
    char* keys[1] = {kBuffersKey};
    char* vals[1] = {kBuffersVal};
    const SoapySDRKwargs streamArgs{1, keys, vals};
    stream_ = a.setupStream(dev_, soapy::kRx, "CS16", chans, 1, &streamArgs);
    if (!stream_)
        throw std::runtime_error("SoapyDevice: setupStream failed: "
                                 + soapyError().toStdString());

    if (a.activateStream(dev_, stream_, 0, 0, 0) != 0) {
        a.closeStream(dev_, stream_);
        stream_ = nullptr;
        throw std::runtime_error("SoapyDevice: activateStream failed: "
                                 + soapyError().toStdString());
    }

    samplesDelivered_ = 0;
    overflowCount_    = 0;
    lastOverflowLog_  = {};
    setState(DeviceState::Streaming);
}

void SoapyDevice::stopStream() {
    const auto& a = soapy::api();
    std::lock_guard lock(apiMutex_);

    if (stream_) {
        a.deactivateStream(dev_, stream_, 0, 0);
        a.closeStream(dev_, stream_);
        stream_ = nullptr;
    }

    // Частота, пришедшая после последнего readBlock, не должна потеряться.
    const double hz = pendingFrequency_.exchange(-1.0);
    if (hz > 0.0 && dev_ && a.setFrequency(dev_, soapy::kRx, 0, hz, nullptr) != 0)
        LOG_WARN("SoapyDevice: deferred setFrequency failed: " + soapyError().toStdString());

    if (state_ == DeviceState::Streaming)
        setState(DeviceState::Ready);
}

void SoapyDevice::applyPendingFrequency() {
    const double hz = pendingFrequency_.exchange(-1.0);
    if (hz <= 0.0) return;

    const auto& a = soapy::api();
    std::lock_guard lock(apiMutex_);
    if (!dev_) return;

    const auto t0 = std::chrono::steady_clock::now();
    if (a.setFrequency(dev_, soapy::kRx, 0, hz, nullptr) != 0) {
        LOG_WARN("SoapyDevice: deferred setFrequency failed: " + soapyError().toStdString());
        return;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (ms > 50)
        LOG_CAT(LogCat::kStreamIo, LogLevel::Warning,
                "SoapyDevice: setFrequency(" + std::to_string(hz) + ") took "
                + std::to_string(ms) + " ms");
}

int SoapyDevice::readBlock(int16_t* buffer, int count, int timeoutMs) {
    const auto& a = soapy::api();
    if (!dev_ || !stream_) return -1;

    applyPendingFrequency();

    void* buffs[1]    = {buffer};
    int   flags       = 0;
    long long timeNs  = 0;
    const int n = a.readStream(dev_, stream_, buffs,
                               static_cast<size_t>(count), &flags, &timeNs,
                               static_cast<long>(timeoutMs) * 1000);

    // Таймаут и overflow не фатальны — воркер продолжает цикл.
    if (n == soapy::kErrOverflow) {
        ++overflowCount_;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastOverflowLog_ > std::chrono::seconds(2)) {
            lastOverflowLog_ = now;
            LOG_WARN("SoapyDevice: RX overflow — samples dropped (total "
                    + std::to_string(overflowCount_) + ") at "
                    + std::to_string(static_cast<int>(sampleRateHz_.load()))
                    + " S/s; USB or DSP cannot keep up");
        }
        return 0;
    }
    if (n == soapy::kErrTimeout)
        return 0;
    if (n < 0)
        return n;

    samplesDelivered_ += static_cast<uint64_t>(n);
    return n;
}

uint64_t SoapyDevice::lastReadTimestamp(ChannelDescriptor /*ch*/) const {
    return samplesDelivered_;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void SoapyDevice::setState(DeviceState s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}
