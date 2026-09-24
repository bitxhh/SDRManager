#include "SpectrumMarkers.h"

#include "qcustomplot.h"

#include <cmath>

namespace {
const QColor kColors[SpectrumMarkers::kMaxMarkers] = {
    QColor(255, 220, 60), QColor(80, 220, 255), QColor(255, 120, 220), QColor(140, 255, 120)};
const char* kKindNames[SpectrumTraces::kKindCount] = {"Live", "Max", "Min", "Avg"};

QString signedNum(double v, int prec) {
    return (v >= 0.0 ? QStringLiteral("+") : QString()) + QString::number(v, 'f', prec);
}
} // namespace

SpectrumMarkers::SpectrumMarkers(QCustomPlot* plot) : plot_(plot) {}

void SpectrumMarkers::setActive(int i) {
    if (i < -1 || i >= count()) return;
    active_ = i;
    restyle();
}

int SpectrumMarkers::add(double mhz, SpectrumTraces::Kind kind) {
    if (count() >= kMaxMarkers) return -1;

    auto* tracer = new QCPItemTracer(plot_);
    tracer->setStyle(QCPItemTracer::tsCircle);
    tracer->setSize(8);
    tracer->position->setType(QCPItemPosition::ptPlotCoords);
    tracer->setSelectable(false);

    auto* label = new QCPItemText(plot_);
    label->position->setParentAnchor(tracer->position);   // смещение в пикселях
    label->position->setCoords(0, -8);
    label->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    label->setPadding(QMargins(3, 1, 3, 1));
    label->setBrush(QColor(0, 0, 0, 170));
    label->setSelectable(false);

    markers_.push_back({mhz, kind, qQNaN(), tracer, label});
    active_ = count() - 1;
    restyle();
    return active_;
}

void SpectrumMarkers::setDeltaRef(int i) {
    deltaRef_ = (i >= 0 && i < count()) ? i : -1;
}

void SpectrumMarkers::remove(int i) {
    if (i < 0 || i >= count()) return;
    if (deltaRef_ == i)     deltaRef_ = -1;
    else if (deltaRef_ > i) --deltaRef_;
    plot_->removeItem(markers_[i].label);
    plot_->removeItem(markers_[i].tracer);
    markers_.remove(i);
    if (active_ >= count() || active_ > i) --active_;
    if (active_ < 0 && count() > 0) active_ = 0;
    restyle();
}

void SpectrumMarkers::clear() {
    while (count() > 0) remove(count() - 1);
}

void SpectrumMarkers::setFreq(int i, double mhz) {
    if (i >= 0 && i < count()) markers_[i].freqMHz = mhz;
}

void SpectrumMarkers::setKind(int i, SpectrumTraces::Kind kind) {
    if (i >= 0 && i < count()) markers_[i].kind = kind;
}

int SpectrumMarkers::nearest(double mhz, double tolMHz) const {
    int best = -1;
    double bestD = tolMHz;
    for (int i = 0; i < count(); ++i) {
        const double d = std::abs(markers_[i].freqMHz - mhz);
        if (d <= bestD) { best = i; bestD = d; }
    }
    return best;
}

void SpectrumMarkers::refresh(const SpectrumTraces& traces) {
    // Сначала уровни всех маркеров, затем подписи: дельта ссылается на опорный.
    for (auto& m : markers_) place(m, traces);
    for (int i = 0; i < count(); ++i) relabel(i);
}

void SpectrumMarkers::place(Marker& m, const SpectrumTraces& traces) {
    const QVector<double>& f = traces.freqMHz();
    const QVector<double>& p = traces.trace(m.kind);
    const int bin = SpectrumPeaks::nearestBin(f, m.freqMHz);
    // Маркер вне текущей оси (сменилась центральная частота) — прячем, но
    // не удаляем: вернётся, когда частота снова попадёт в полосу.
    const bool ok = bin >= 0 && bin < p.size()
                 && m.freqMHz >= f.first() && m.freqMHz <= f.last();
    m.levelDb = ok ? p[bin] : qQNaN();
    m.tracer->setVisible(ok);
    m.label->setVisible(ok);
    if (!ok) return;

    m.tracer->position->setCoords(m.freqMHz, m.levelDb);
}

void SpectrumMarkers::relabel(int i) {
    const Marker& m = markers_[i];
    if (!std::isfinite(m.levelDb)) return;
    QString head = QString("M%1 %2").arg(i + 1).arg(kKindNames[m.kind]);
    QString body = QString("%1 MHz  %2 dB").arg(m.freqMHz, 0, 'f', 4).arg(m.levelDb, 0, 'f', 1);
    if (deltaRef_ == i) {
        head += "  REF";
    } else if (deltaRef_ >= 0 && std::isfinite(markers_[deltaRef_].levelDb)) {
        const Marker& r  = markers_[deltaRef_];
        const double  df = (m.freqMHz - r.freqMHz) * 1e3;   // kHz
        body = QString("\u0394M%1 %2 kHz  %3 dB")
                   .arg(deltaRef_ + 1)
                   .arg(signedNum(df, std::abs(df) < 100.0 ? 2 : 1))
                   .arg(signedNum(m.levelDb - r.levelDb, 1));
    }
    m.label->setText(head + "\n" + body);
}

void SpectrumMarkers::restyle() {
    for (int i = 0; i < count(); ++i) {
        const QColor c = kColors[i];
        const bool   a = i == active_;
        auto& m = markers_[i];
        m.tracer->setPen(QPen(c, a ? 2.0 : 1.2));
        m.tracer->setBrush(a ? QBrush(c) : Qt::NoBrush);
        m.label->setColor(c);
        m.label->setPen(a ? QPen(c) : Qt::NoPen);
        QFont fnt = m.label->font();
        fnt.setBold(a);
        m.label->setFont(fnt);
    }
}
