#include "ChannelModem.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
ChannelModem::ChannelModem(double inputSR, double stationOffsetHz,
                           double fir1CutoffHz, double fir2CutoffHz,
                           double minIfHz,
                           int fir1Taps, int fir2Taps)
    : inputSR_(inputSR)
    , stationOffset_(stationOffsetHz)
    , bandwidth_(0.0)
    , fir1Taps_(fir1Taps)
    , fir2Taps_(fir2Taps)
{
    // Если inputSR кратен 480 кГц — берём ровно 480 кГц IF (→ аудио 48 кГц,
    // без ресемплинга в выходе), иначе ближайшее к 500 кГц целое деление.
    const double r48 = inputSR_ / 480'000.0;
    if (r48 >= 1.0 && std::abs(r48 - std::round(r48)) < 1e-6)
        D1_ = static_cast<int>(std::round(r48));
    else
        D1_ = std::max(1, static_cast<int>(std::round(inputSR_ / 500'000.0)));
    ifSR_ = inputSR_ / D1_;

    if (ifSR_ < minIfHz)
        throw std::invalid_argument(
            std::string("Modem: IF rate ") + std::to_string(static_cast<int>(ifSR_))
            + " Hz is too low (need >= " + std::to_string(static_cast<int>(minIfHz))
            + " Hz). Raise the device sample rate.");

    audioSR_ = ifSR_ / static_cast<double>(D2_);

    // ── Halfband ÷2 каскад: снимаем множители 2 из D1, пока FIR1 остаётся
    // хотя бы ÷2 (иначе ему нечего подавлять между IF/2 и stageSR/2).
    D1r_ = D1_;
    int k = 0;
    while (D1r_ % 2 == 0 && D1r_ / 2 >= 2) { D1r_ /= 2; ++k; }
    stageSR_ = inputSR_ / static_cast<double>(1 << k);

    const auto hbCoeffs = dsp::designLowpassFir(kHalfbandTaps, 0.25);
    const double hbMax  = *std::max_element(hbCoeffs.begin(), hbCoeffs.end());
    hb_.resize(k);
    for (auto& st : hb_) {
        for (int i = 0; i < kHalfbandTaps; ++i)
            if (std::abs(hbCoeffs[i]) > 1e-9 * hbMax) {
                st.idx.push_back(i);
                st.coef.push_back(hbCoeffs[i]);
            }
        st.delay.assign(2 * kHalfbandTaps, {0.0, 0.0});
    }

    // ── FIR1: complex anti-alias lowpass (работает на stageSR) ────────────────
    fir1Delay_.assign(2 * fir1Taps_, {0.0, 0.0});   // зеркальная линия, см. fir1Push
    fir1Coeffs_ = dsp::designLowpassFir(fir1Taps_, clampFir1Cutoff(fir1CutoffHz) / stageSR_);

    // ── FIR2: real audio lowpass ─────────────────────────────────────────────
    fir2Delay_.assign(2 * fir2Taps_, 0.0);
    const double cutoff2 = std::min(fir2CutoffHz, audioSR_ / 2.0 * 0.9);
    fir2Coeffs_ = dsp::designLowpassFir(fir2Taps_, cutoff2 / ifSR_);

    // ── NCO ──────────────────────────────────────────────────────────────────
    nco_.setFrequency(stationOffset_, inputSR_);
}

// ---------------------------------------------------------------------------
// redesignFir1 / redesignFir2
// ---------------------------------------------------------------------------
// Срез FIR1 выше IF/2 = алиасинг соседних каналов в IF после ÷D1r.
double ChannelModem::clampFir1Cutoff(double cutoffHz) const {
    return std::min(cutoffHz, ifSR_ / 2.0 * 0.95);
}

void ChannelModem::redesignFir1(double cutoffHz) {
    fir1Coeffs_ = dsp::designLowpassFir(fir1Taps_, clampFir1Cutoff(cutoffHz) / stageSR_);
    std::fill(fir1Delay_.begin(), fir1Delay_.end(), std::complex<double>{0.0, 0.0});
    fir1Head_ = 0;
}

void ChannelModem::redesignFir2(double cutoffHz) {
    const double cutoff = std::min(cutoffHz, audioSR_ / 2.0 * 0.9);
    fir2Coeffs_ = dsp::designLowpassFir(fir2Taps_, cutoff / ifSR_);
    std::fill(fir2Delay_.begin(), fir2Delay_.end(), 0.0);
    fir2Head_ = 0;
}

// ---------------------------------------------------------------------------
// setOffset
// ---------------------------------------------------------------------------
void ChannelModem::setOffset(double offsetHz) {
    stationOffset_ = offsetHz;
    nco_.setFrequency(stationOffset_, inputSR_);

    dc_.reset();
    for (auto& st : hb_) st.reset();

    std::fill(fir1Delay_.begin(), fir1Delay_.end(), std::complex<double>{0.0, 0.0});
    fir1Head_    = 0;
    dec1Counter_ = 0;

    std::fill(fir2Delay_.begin(), fir2Delay_.end(), 0.0);
    fir2Head_    = 0;
    dec2Counter_ = 0;

    resetDemodState();
    for (auto& p : audioChain_) p->reset();

    LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
            std::string(modemName()) + ": offset set to "
            + std::to_string(static_cast<int>(offsetHz)) + " Hz");
}

// ---------------------------------------------------------------------------
// Halfband ÷2
// ---------------------------------------------------------------------------
// У halfband-фильтра каждый второй тап (кроме центрального) равен нулю, а
// выход нужен только на каждом втором входе: ~12 MAC на входной отсчёт
// вместо 47.
bool ChannelModem::HalfbandStage::push(std::complex<double> x,
                                       std::complex<double>& y) {
    delay[head]                 = x;
    delay[head + kHalfbandTaps] = x;
    if (++head == kHalfbandTaps) head = 0;

    odd = !odd;
    if (odd) return false;

    const std::complex<double>* d = delay.data() + head;
    const int n = static_cast<int>(idx.size());
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i) {
        const auto v = d[idx[i]];
        re += coef[i] * v.real();
        im += coef[i] * v.imag();
    }
    y = {re, im};
    return true;
}

void ChannelModem::HalfbandStage::reset() {
    std::fill(delay.begin(), delay.end(), std::complex<double>{0.0, 0.0});
    head = 0;
    odd  = false;
}

// ---------------------------------------------------------------------------
// FIR1
// ---------------------------------------------------------------------------
// Линия задержки хранит каждый отсчёт дважды (head и head+taps), поэтому
// окно [head, head+taps) всегда непрерывно: свёртка — плоский dot product
// без `% taps` на каждом тапе (idiv в цепочке зависимостей съедал ядро
// на 3.2 MS/s) и векторизуется компилятором.
void ChannelModem::fir1Push(std::complex<double> x) {
    fir1Delay_[fir1Head_]             = x;
    fir1Delay_[fir1Head_ + fir1Taps_] = x;
    if (++fir1Head_ == fir1Taps_) fir1Head_ = 0;
}

std::complex<double> ChannelModem::fir1Compute() const {
    const double*               c = fir1Coeffs_.data();
    const std::complex<double>* d = fir1Delay_.data() + fir1Head_;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < fir1Taps_; ++i) {
        re += c[i] * d[i].real();
        im += c[i] * d[i].imag();
    }
    return {re, im};
}

// ---------------------------------------------------------------------------
// FIR2
// ---------------------------------------------------------------------------
// Push на каждом IF-отсчёте, свёртка — только на выходных (÷D2): в 10 раз
// меньше MAC, чем при фильтрации каждого отсчёта с последующим выбрасыванием.
void ChannelModem::fir2Push(double x) {
    fir2Delay_[fir2Head_]             = x;
    fir2Delay_[fir2Head_ + fir2Taps_] = x;
    if (++fir2Head_ == fir2Taps_) fir2Head_ = 0;
}

double ChannelModem::fir2Compute() const {
    const double* c = fir2Coeffs_.data();
    const double* d = fir2Delay_.data() + fir2Head_;
    double acc = 0.0;
    for (int i = 0; i < fir2Taps_; ++i)
        acc += c[i] * d[i];
    return acc;
}

// ---------------------------------------------------------------------------
// produceAudio — default path: real demodulateIF() → FIR2 → decimate D2.
// SSB/CW override this to run their own complex decimation + audio filter.
// ---------------------------------------------------------------------------
void ChannelModem::produceAudio(std::complex<double> ifSample, double ifPower,
                                QVector<float>& out) {
    // demodulateIF — на каждом IF-отсчёте: дискриминатор/де-эмфазис/PLL
    // держат состояние между отсчётами.
    fir2Push(demodulateIF(ifSample, ifPower));

    if (++dec2Counter_ < D2_) return;
    dec2Counter_ = 0;

    out.push_back(static_cast<float>(fir2Compute()));
}

// ---------------------------------------------------------------------------
// Main processing
// ---------------------------------------------------------------------------
QVector<float> ChannelModem::pushBlock(const float* iq, int count) {
    if (count < 1)
        return {};

    const int numSamples = count;

    QVector<float> audio;
    audio.reserve(numSamples / (D1_ * D2_) + 4);

    for (int i = 0; i < numSamples; ++i) {

        // ── 1. Normalised float32 → complex double ────────────────────────────
        const double iVal = static_cast<double>(iq[2 * i]);
        const double qVal = static_cast<double>(iq[2 * i + 1]);
        std::complex<double> s{iVal, qVal};

        // ── 2. DC blocker ────────────────────────────────────────────────────
        s = dc_.process(s);

        // ── 2a. Impulse noise blanker (wideband, before any filter smears it) ─
        s = nb_.process(s);

        // ── 3. NCO frequency shift ───────────────────────────────────────────
        s = nco_.mix(s);

        // ── 4. Halfband ÷2 cascade ───────────────────────────────────────────
        bool ready = true;
        for (auto& st : hb_)
            if (!st.push(s, s)) { ready = false; break; }
        if (!ready) continue;

        // ── 5. FIR1 push (O(1)) ─────────────────────────────────────────────
        fir1Push(s);

        // ── 6. Stage-1 decimation ────────────────────────────────────────────
        if (++dec1Counter_ < D1r_) continue;
        dec1Counter_ = 0;

        const auto filtered1 = fir1Compute();

        // ── 7. IF power |IF|² (AM envelope etc.) ─────────────────────────────
        const double ifPower = filtered1.real() * filtered1.real()
                             + filtered1.imag() * filtered1.imag();

        // ── 8-10. Subclass audio production (demod → FIR2 → decimate D2) ──────
        produceAudio(filtered1, ifPower, audio);
    }

    // ── 11. Post-demod audio chain ────────────────────────────────────────────
    if (!audio.isEmpty())
        for (auto& p : audioChain_) p->process(audio.data(), static_cast<int>(audio.size()));

    return audio;
}

// ---------------------------------------------------------------------------
// Audio processor chain
// ---------------------------------------------------------------------------
void ChannelModem::addAudioProcessor(std::unique_ptr<IAudioProcessor> p) {
    if (!p) return;
    p->prepare(audioSR_);
    audioChain_.push_back(std::move(p));
}

bool ChannelModem::setCommonParam(const QString& name, double value) {
    if (name == QLatin1String(kNbThresholdKey)) {
        nbThreshold_ = std::max(0.0, value);
    } else if (name == QLatin1String(kNbWidthKey)) {
        nbWidthUs_ = std::max(0.0, value);
    } else {
        return setAudioParam(name, value);
    }
    nb_.configure(inputSR_, nbThreshold_, nbWidthUs_ * 1e-6);
    return true;
}

bool ChannelModem::setAudioParam(const QString& name, double value) {
    bool used = false;
    for (auto& p : audioChain_) used = p->setParam(name, value) || used;
    return used;
}
