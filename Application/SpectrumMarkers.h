#pragma once

#include "../Core/SpectrumTraces.h"

#include <QVector>

class QCustomPlot;
class QCPItemTracer;
class QCPItemText;

// ---------------------------------------------------------------------------
// SpectrumMarkers — до kMaxMarkers маркеров на графике спектра (8.3).
// Маркер хранит частоту (MHz) и трассу SpectrumTraces::Kind; уровень берётся
// из ближайшего бина этой трассы в refresh(). QCPItemTracer не привязан к
// QCPGraph — скрытые трассы (данные графика очищены) тоже измеряются.
// Позиция в координатах графика → при зуме маркер остаётся на своей частоте.
// ---------------------------------------------------------------------------
class SpectrumMarkers {
public:
    static constexpr int kMaxMarkers = 4;

    explicit SpectrumMarkers(QCustomPlot* plot);

    [[nodiscard]] int  count()  const { return static_cast<int>(markers_.size()); }
    [[nodiscard]] int  active() const { return active_; }         // -1 = нет маркеров
    void setActive(int i);

    int  add(double mhz, SpectrumTraces::Kind kind);              // -1 = лимит
    void remove(int i);
    void clear();

    void setFreq(int i, double mhz);
    [[nodiscard]] double freqMHz(int i) const { return markers_[i].freqMHz; }
    void setKind(int i, SpectrumTraces::Kind kind);
    [[nodiscard]] SpectrumTraces::Kind kind(int i) const { return markers_[i].kind; }
    [[nodiscard]] double levelDb(int i) const { return markers_[i].levelDb; }   // NaN = нет данных

    // Дельта-режим (8.4): остальные маркеры показывают Δf/ΔdB относительно
    // опорного. -1 = выключен; сбрасывается при удалении опорного маркера.
    void setDeltaRef(int i);
    [[nodiscard]] int deltaRef() const { return deltaRef_; }

    // Индекс ближайшего маркера в пределах tolMHz, иначе -1.
    [[nodiscard]] int nearest(double mhz, double tolMHz) const;

    // Пересчитать уровни/подписи по текущим трассам (каждый кадр FFT).
    void refresh(const SpectrumTraces& traces);

private:
    struct Marker {
        double               freqMHz;
        SpectrumTraces::Kind kind;
        double               levelDb;
        QCPItemTracer*       tracer;
        QCPItemText*         label;
    };
    void restyle();                     // номера M1..Mn, цвета, выделение активного
    void place(Marker& m, const SpectrumTraces& traces);
    void relabel(int i);

    QCustomPlot*    plot_;
    QVector<Marker> markers_;
    int             active_{-1};
    int             deltaRef_{-1};
};
