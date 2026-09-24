#pragma once

#include <QSettings>
#include <QVector>
#include <algorithm>
#include <array>

// ---------------------------------------------------------------------------
// SpectrumTraces — накопитель трасс спектра (Live / Max hold / Min hold /
// Average) для страницы Радиомониторинг. Чистая логика без GUI: живёт на
// GUI-потоке, кормится кадрами FFT из RadioMonitorPage::onFftReady.
//
// Average — экспоненциальное усреднение мощности в линейной шкале (не в dB:
// усреднение логарифма занижает шум на ~2.5 дБ). Первые 1/α кадров идут как
// обычное среднее, чтобы трасса не «выплывала» из первого кадра.
//
// Все трассы сбрасываются при смене оси частот (центральная частота, sample
// rate, размер FFT) — сравниваются число бинов и крайние частоты.
// ---------------------------------------------------------------------------
class SpectrumTraces {
public:
    enum Kind { Live = 0, MaxHold, MinHold, Average, kKindCount };

    void setAverageAlpha(double alpha);             // clamped to (0, 1]
    [[nodiscard]] double averageAlpha() const { return alpha_; }

    void update(const QVector<double>& freqMHz, const QVector<double>& powerDb);
    void clear();                                   // сброс накопленного, ось сохраняется

    [[nodiscard]] const QVector<double>& freqMHz() const { return freqMHz_; }
    [[nodiscard]] const QVector<double>& trace(Kind k) const { return traces_[k]; }
    [[nodiscard]] int frameCount() const { return frames_; }   // кадров с последнего сброса

private:
    [[nodiscard]] bool sameAxis(const QVector<double>& freqMHz) const;

    QVector<double>                     freqMHz_;
    std::array<QVector<double>, kKindCount> traces_;
    QVector<double>                     avgLin_;    // средняя мощность, линейно
    double                              alpha_  = 0.1;
    int                                 frames_ = 0;
};

// ---------------------------------------------------------------------------
// Поиск пиков для маркеров (8.3). Диапазон [first, last] включительно,
// обрезается по размеру трассы; -1 — пустой диапазон / пик не найден.
// ---------------------------------------------------------------------------
namespace SpectrumPeaks {
// Индекс максимума в диапазоне.
[[nodiscard]] int peakIndex(const QVector<double>& powerDb, int first, int last);
// Самый высокий локальный максимум строго ниже belowDb («Next peak»).
// Локальный максимум: не ниже соседей и выше хотя бы одного из них (плато
// даёт один пик); крайние бины диапазона сравниваются с одним соседом.
[[nodiscard]] int nextPeakIndex(const QVector<double>& powerDb, int first, int last,
                                double belowDb);
// Ближайший к freqMHz бин (ось отсортирована по возрастанию); -1 — пустая ось.
[[nodiscard]] int nearestBin(const QVector<double>& freqMHz, double mhz);
} // namespace SpectrumPeaks

// ---------------------------------------------------------------------------
// SpectrumTraceSettings — какие трассы видны и коэффициент усреднения.
// Persisted globally in QSettings under "spectrum/*" (same pattern as
// WaterfallSettings — display preference, not a per-device setting).
// ---------------------------------------------------------------------------
struct SpectrumTraceSettings {
    std::array<bool, SpectrumTraces::kKindCount> visible{true, false, false, false};
    int averageFrames = 10;                         // α = 1 / averageFrames

    static constexpr int kMinAverageFrames = 2;
    static constexpr int kMaxAverageFrames = 200;

    [[nodiscard]] static SpectrumTraceSettings load();
    void save() const;
};

inline SpectrumTraceSettings SpectrumTraceSettings::load() {
    QSettings s(QStringLiteral("SDRManager"), QStringLiteral("SDRManager"));
    SpectrumTraceSettings t;
    t.visible[SpectrumTraces::Live]    = s.value("spectrum/live",    t.visible[0]).toBool();
    t.visible[SpectrumTraces::MaxHold] = s.value("spectrum/maxHold", t.visible[1]).toBool();
    t.visible[SpectrumTraces::MinHold] = s.value("spectrum/minHold", t.visible[2]).toBool();
    t.visible[SpectrumTraces::Average] = s.value("spectrum/average", t.visible[3]).toBool();
    t.averageFrames = std::clamp(s.value("spectrum/averageFrames", t.averageFrames).toInt(),
                                 kMinAverageFrames, kMaxAverageFrames);
    return t;
}

inline void SpectrumTraceSettings::save() const {
    QSettings s(QStringLiteral("SDRManager"), QStringLiteral("SDRManager"));
    s.setValue("spectrum/live",          visible[SpectrumTraces::Live]);
    s.setValue("spectrum/maxHold",       visible[SpectrumTraces::MaxHold]);
    s.setValue("spectrum/minHold",       visible[SpectrumTraces::MinHold]);
    s.setValue("spectrum/average",       visible[SpectrumTraces::Average]);
    s.setValue("spectrum/averageFrames", averageFrames);
}
