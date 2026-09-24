#include "SpectrumTraces.h"

#include <cmath>

void SpectrumTraces::setAverageAlpha(double alpha) {
    alpha_ = std::clamp(alpha, 1e-4, 1.0);
}

bool SpectrumTraces::sameAxis(const QVector<double>& freqMHz) const {
    if (freqMHz.size() != freqMHz_.size()) return false;
    if (freqMHz.isEmpty()) return true;
    // Сдвиг меньше 1e-9 МГц (1 мГц) — та же ось; допуск на округление double.
    constexpr double kTolMHz = 1e-9;
    return std::abs(freqMHz.first() - freqMHz_.first()) < kTolMHz
        && std::abs(freqMHz.last()  - freqMHz_.last())  < kTolMHz;
}

void SpectrumTraces::clear() {
    frames_ = 0;
}

void SpectrumTraces::update(const QVector<double>& freqMHz, const QVector<double>& powerDb) {
    const int n = std::min(freqMHz.size(), powerDb.size());
    if (n != freqMHz.size() || !sameAxis(freqMHz)) {
        freqMHz_ = freqMHz.mid(0, n);
        frames_  = 0;
    }
    if (n == 0) return;

    const bool first = (frames_ == 0);
    if (first) {
        for (auto& t : traces_) t.resize(n);
        avgLin_.resize(n);
    }
    ++frames_;

    // Прогрев: пока кадров меньше 1/α, берём обычное среднее (вес 1/frames).
    const double a = std::max(alpha_, 1.0 / frames_);

    double* live = traces_[Live].data();
    double* mx   = traces_[MaxHold].data();
    double* mn   = traces_[MinHold].data();
    double* avg  = traces_[Average].data();
    double* lin  = avgLin_.data();
    const double* p = powerDb.constData();

    for (int i = 0; i < n; ++i) {
        const double db = p[i];
        const double pl = std::pow(10.0, db * 0.1);
        live[i] = db;
        if (first) {
            mx[i]  = db;
            mn[i]  = db;
            lin[i] = pl;
        } else {
            mx[i]  = std::max(mx[i], db);
            mn[i]  = std::min(mn[i], db);
            lin[i] += a * (pl - lin[i]);
        }
        avg[i] = 10.0 * std::log10(lin[i] + 1e-30);
    }
}

// ---------------------------------------------------------------------------
namespace SpectrumPeaks {

namespace {
bool clampRange(const QVector<double>& v, int& first, int& last) {
    first = std::max(first, 0);
    last  = std::min(last, static_cast<int>(v.size()) - 1);
    return first <= last;
}
} // namespace

int peakIndex(const QVector<double>& powerDb, int first, int last) {
    if (!clampRange(powerDb, first, last)) return -1;
    const auto b = powerDb.cbegin();
    return static_cast<int>(std::max_element(b + first, b + last + 1) - b);
}

int nextPeakIndex(const QVector<double>& powerDb, int first, int last, double belowDb) {
    if (!clampRange(powerDb, first, last)) return -1;
    int best = -1;
    for (int i = first; i <= last; ++i) {
        const double v = powerDb[i];
        if (!(v < belowDb)) continue;
        const bool hasL = i > first, hasR = i < last;
        const double l = hasL ? powerDb[i - 1] : -INFINITY;
        const double r = hasR ? powerDb[i + 1] : -INFINITY;
        // Плато: берём его левый край (v > l), правый сосед может быть равен.
        if (!(v > l && v >= r)) continue;
        if (!hasL && !hasR) continue;           // один бин — не пик
        if (best < 0 || v > powerDb[best]) best = i;
    }
    return best;
}

int nearestBin(const QVector<double>& freqMHz, double mhz) {
    if (freqMHz.isEmpty()) return -1;
    const auto it = std::lower_bound(freqMHz.cbegin(), freqMHz.cend(), mhz);
    if (it == freqMHz.cbegin()) return 0;
    if (it == freqMHz.cend())   return static_cast<int>(freqMHz.size()) - 1;
    const int hi = static_cast<int>(it - freqMHz.cbegin());
    return (mhz - freqMHz[hi - 1] <= freqMHz[hi] - mhz) ? hi - 1 : hi;
}

} // namespace SpectrumPeaks
