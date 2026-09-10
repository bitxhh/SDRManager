#include "WaterfallView.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace {

struct Anchor { float pos; quint8 r, g, b; };

const Anchor kClassic[] = {
    {0.00f,   0,   0,   0}, {0.20f,   0,   0, 140}, {0.40f,   0, 180, 220},
    {0.55f,   0, 200,  60}, {0.70f, 230, 230,   0}, {0.85f, 255,  60,   0},
    {1.00f, 255, 255, 255},
};
const Anchor kViridis[] = {
    {0.00f,  68,   1,  84}, {0.25f,  59,  82, 139}, {0.50f,  33, 145, 140},
    {0.75f,  94, 201,  98}, {1.00f, 253, 231,  37},
};
const Anchor kInferno[] = {
    {0.00f,   0,   0,   4}, {0.25f,  87,  16, 110}, {0.50f, 188,  55,  84},
    {0.75f, 249, 142,   9}, {1.00f, 252, 255, 164},
};
const Anchor kTurbo[] = {
    {0.00f,  48,  18,  59}, {0.17f,  62, 156, 254}, {0.33f,  24, 215, 203},
    {0.50f,  70, 247,  90}, {0.67f, 225, 220,  55}, {0.83f, 254, 108,  25},
    {1.00f, 122,   4,   3},
};

QVector<QRgb> lutFromAnchors(const Anchor* a, int n) {
    QVector<QRgb> lut(256);
    for (int i = 0; i < 256; ++i) {
        const float t = float(i) / 255.0f;
        int seg = 0;
        while (seg < n - 2 && t > a[seg + 1].pos) ++seg;
        const Anchor& lo   = a[seg];
        const Anchor& hi   = a[seg + 1];
        const float   span = hi.pos - lo.pos;
        const float   f    = span > 0.0f ? std::clamp((t - lo.pos) / span, 0.0f, 1.0f) : 0.0f;
        lut[i] = qRgb(int(lo.r + f * (hi.r - lo.r)),
                      int(lo.g + f * (hi.g - lo.g)),
                      int(lo.b + f * (hi.b - lo.b)));
    }
    return lut;
}

const QColor kBackground(30, 30, 30);

} // namespace

WaterfallView::WaterfallView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(80);
    setMouseTracking(true);   // hover-курсор над полосами фильтров
    rebuildLut();
}

void WaterfallView::applySettings(const WaterfallSettings& s) {
    const bool recolor = s.colormap != settings_.colormap
                      || s.dbMin    != settings_.dbMin
                      || s.dbMax    != settings_.dbMax;
    const bool reinit  = s.historyDepth != settings_.historyDepth;
    settings_ = s;
    rebuildLut();
    if (reinit && bins_ > 0)
        reinitBuffers(bins_, settings_.historyDepth);   // drops history
    else if (recolor && rows_ > 0)
        renderAllFromHistory();
    update();
}

void WaterfallView::setVisibleFreqRange(double loMHz, double hiMHz) {
    if (loMHz == visLoMHz_ && hiMHz == visHiMHz_)
        return;
    visLoMHz_ = loMHz;
    visHiMHz_ = hiMHz;
    update();
}

void WaterfallView::setEdgeMargins(int leftPx, int rightPx) {
    leftPx  = std::max(0, leftPx);
    rightPx = std::max(0, rightPx);
    if (leftPx == marginLeft_ && rightPx == marginRight_)
        return;
    marginLeft_  = leftPx;
    marginRight_ = rightPx;
    update();
}

void WaterfallView::setFilterBands(const QVector<Band>& bands) {
    bands_ = bands;
    update();
}

void WaterfallView::clearHistory() {
    rows_   = 0;
    topRow_ = 0;
    if (!image_.isNull())
        image_.fill(kBackground);
    update();
}

void WaterfallView::appendLine(const WaterfallLine& line) {
    const int bins = int(line.powerDb.size());
    if (bins <= 0)
        return;
    if (bins != bins_ || depth_ != settings_.historyDepth)
        reinitBuffers(bins, settings_.historyDepth);

    const double spanMHz = line.sampleRateHz / 1e6;
    if (line.centerFreqMHz != centerMHz_ || spanMHz != spanMHz_) {
        // Frequency axis changed — old lines no longer align with the new axis.
        centerMHz_ = line.centerFreqMHz;
        spanMHz_   = spanMHz;
        clearHistory();
    }

    topRow_ = (topRow_ - 1 + depth_) % depth_;
    std::memcpy(history_.data() + size_t(topRow_) * size_t(bins_),
                line.powerDb.constData(), size_t(bins_) * sizeof(float));
    colorizeRow(topRow_, line.powerDb.constData());
    rows_ = std::min(rows_ + 1, depth_);
    update();
}

void WaterfallView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kBackground);

    // Drawable area aligned with the spectrum plot's axis rect.
    const QRectF dst(marginLeft_, 0.0,
                     width() - marginLeft_ - marginRight_, height());
    if (dst.width() <= 0.0)
        return;

    // Visible frequency range: spectrum zoom, else full stored span.
    double visLo = visLoMHz_, visHi = visHiMHz_;
    if (visHi <= visLo) {              // zoom range not set yet — full span
        if (spanMHz_ <= 0.0)
            return;
        visLo = centerMHz_ - spanMHz_ / 2.0;
        visHi = visLo + spanMHz_;
    }

    if (rows_ > 0 && bins_ > 0 && spanMHz_ > 0.0) {
        // Horizontal crop: visible frequency range → source columns of the image.
        const double fullLo     = centerMHz_ - spanMHz_ / 2.0;
        const double binsPerMHz = bins_ / spanMHz_;
        const double sxLo = std::clamp((visLo - fullLo) * binsPerMHz, 0.0, double(bins_));
        const double sxHi = std::clamp((visHi - fullLo) * binsPerMHz, 0.0, double(bins_));
        const double sxW  = sxHi - sxLo;
        if (sxW > 0.0) {
            p.setRenderHint(QPainter::SmoothPixmapTransform);

            // Circular buffer → at most two contiguous segments, newest row on top.
            const int visibleRows = std::min(rows_, height());
            const int seg1        = std::min(visibleRows, depth_ - topRow_);
            p.drawImage(QRectF(dst.left(), 0, dst.width(), seg1),
                        image_, QRectF(sxLo, topRow_, sxW, seg1));
            if (visibleRows > seg1)
                p.drawImage(QRectF(dst.left(), seg1, dst.width(), visibleRows - seg1),
                            image_, QRectF(sxLo, 0, sxW, visibleRows - seg1));
        }
    }

    drawFilterBands(p, dst, visLo, visHi);
}

void WaterfallView::drawFilterBands(QPainter& p, const QRectF& dst,
                                    double visLo, double visHi) const {
    if (bands_.isEmpty() || visHi <= visLo)
        return;
    const double pxPerMHz = dst.width() / (visHi - visLo);
    // Same colours as the VFO band overlay on the spectrum plot.
    const QColor fill(0, 200, 80, 40);
    const QPen   edge(QColor(0, 200, 80, 160), 1.0);
    for (const Band& b : bands_) {
        const double x1 = dst.left() + (b.loMHz - visLo) * pxPerMHz;
        const double x2 = dst.left() + (b.hiMHz - visLo) * pxPerMHz;
        const QRectF r  = QRectF(QPointF(x1, dst.top()),
                                 QPointF(x2, dst.bottom())) & dst;
        if (r.isEmpty())
            continue;
        p.fillRect(r, fill);
        p.setPen(edge);
        if (x1 >= dst.left() && x1 <= dst.right())
            p.drawLine(QLineF(x1, dst.top(), x1, dst.bottom()));
        if (x2 >= dst.left() && x2 <= dst.right())
            p.drawLine(QLineF(x2, dst.top(), x2, dst.bottom()));
    }
}

void WaterfallView::rebuildLut() {
    using CM = WaterfallSettings::Colormap;
    switch (settings_.colormap) {
    case CM::Viridis:   lut_ = lutFromAnchors(kViridis, int(std::size(kViridis))); break;
    case CM::Inferno:   lut_ = lutFromAnchors(kInferno, int(std::size(kInferno))); break;
    case CM::Turbo:     lut_ = lutFromAnchors(kTurbo,   int(std::size(kTurbo)));   break;
    case CM::Grayscale: {
        lut_.resize(256);
        for (int i = 0; i < 256; ++i) lut_[i] = qRgb(i, i, i);
        break;
    }
    case CM::ClassicSdr:
    default:            lut_ = lutFromAnchors(kClassic, int(std::size(kClassic))); break;
    }
}

void WaterfallView::reinitBuffers(int bins, int depth) {
    bins_  = bins;
    depth_ = depth;
    image_ = QImage(bins_, depth_, QImage::Format_RGB32);
    image_.fill(kBackground);
    history_.assign(size_t(bins_) * size_t(depth_), -300.0f);
    topRow_ = 0;
    rows_   = 0;
}

void WaterfallView::colorizeRow(int row, const float* db) {
    const float dbMin = float(settings_.dbMin);
    const float scale = 255.0f / float(settings_.dbMax - settings_.dbMin);
    auto* line = reinterpret_cast<QRgb*>(image_.scanLine(row));
    for (int x = 0; x < bins_; ++x) {
        const int idx = std::clamp(int((db[x] - dbMin) * scale), 0, 255);
        line[x] = lut_[idx];
    }
}

void WaterfallView::renderAllFromHistory() {
    for (int i = 0; i < rows_; ++i) {
        const int row = (topRow_ + i) % depth_;
        colorizeRow(row, history_.data() + size_t(row) * size_t(bins_));
    }
}

// ---------------------------------------------------------------------------
// Mouse → frequency. Same visible-range mapping as paintEvent's crop.
// ---------------------------------------------------------------------------
double WaterfallView::xToMHz(double x) const {
    const double w = width() - marginLeft_ - marginRight_;
    if (w <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    double visLo = visLoMHz_, visHi = visHiMHz_;
    if (visHi <= visLo) {              // zoom range not set yet — full span
        if (spanMHz_ <= 0.0)
            return std::numeric_limits<double>::quiet_NaN();
        visLo = centerMHz_ - spanMHz_ / 2.0;
        visHi = visLo + spanMHz_;
    }
    const double t = std::clamp((x - marginLeft_) / w, 0.0, 1.0);
    return visLo + t * (visHi - visLo);
}

void WaterfallView::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) {
        const double mhz = xToMHz(ev->position().x());
        if (!std::isnan(mhz)) emit freqPressed(mhz);
    }
    QWidget::mousePressEvent(ev);
}

void WaterfallView::mouseMoveEvent(QMouseEvent* ev) {
    const double mhz = xToMHz(ev->position().x());
    if (!std::isnan(mhz)) {
        if (ev->buttons() & Qt::LeftButton) emit freqDragged(mhz);
        else                                emit freqHovered(mhz);
    }
    QWidget::mouseMoveEvent(ev);
}

void WaterfallView::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton)
        emit freqReleased();
    QWidget::mouseReleaseEvent(ev);
}
