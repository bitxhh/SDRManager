#pragma once

#include "../Core/WaterfallSettings.h"
#include "../DSP/WaterfallHandler.h"   // WaterfallLine

#include <QImage>
#include <QVector>
#include <QWidget>

#include <vector>

class QPainter;

// ---------------------------------------------------------------------------
// WaterfallView — draws the waterfall under the spectrum plot.
//
// Rendering: a QImage (bins × historyDepth) used as a circular row buffer —
// topRow_ is the newest line; new lines are written at
// (topRow_ - 1 + depth) % depth, so there is no per-frame scroll copy.
// paintEvent draws at most two source-rect segments, 1 line = 1 pixel row,
// newest at the top. A parallel float-dB history ring allows recoloring on
// colormap / dB-range change without re-running any FFTs.
//
// Zoom follow: setVisibleFreqRange() crops the horizontal source rect from
// the full-span image, so the waterfall tracks the spectrum plot's x-axis.
// setEdgeMargins() reserves left/right pixel margins matching the spectrum
// plot's axis rect, so frequencies line up column-for-column with the plot.
//
// appendLine() stores every incoming line and calls update(); Qt coalesces
// repaints, so painting late never loses data.
//
// Overlay: setFilterBands() draws the demodulator VFO bands (same colours as
// the spectrum overlay) on top of the waterfall image.
//
// Mouse: the view only translates pixels → MHz (same mapping as paintEvent)
// and emits freq* signals; tuning/drag policy lives in RadioMonitorPage.
// ---------------------------------------------------------------------------
class WaterfallView : public QWidget {
    Q_OBJECT
public:
    struct Band { double loMHz = 0.0; double hiMHz = 0.0; };

    explicit WaterfallView(QWidget* parent = nullptr);

    void applySettings(const WaterfallSettings& s);
    void setVisibleFreqRange(double loMHz, double hiMHz);
    void setEdgeMargins(int leftPx, int rightPx);
    void setFilterBands(const QVector<Band>& bands);
    void clearHistory();

public slots:
    void appendLine(const WaterfallLine& line);

signals:
    void freqPressed(double mhz);    // ЛКМ нажата
    void freqDragged(double mhz);    // движение с зажатой ЛКМ
    void freqReleased();             // ЛКМ отпущена
    void freqHovered(double mhz);    // движение без кнопок (mouse tracking)

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

private:
    void rebuildLut();
    void reinitBuffers(int bins, int depth);
    void colorizeRow(int row, const float* db);
    void renderAllFromHistory();
    void drawFilterBands(QPainter& p, const QRectF& dst,
                         double visLo, double visHi) const;
    [[nodiscard]] double xToMHz(double x) const;   // NaN, если данных ещё нет

    WaterfallSettings  settings_;
    QVector<QRgb>      lut_;         // 256-entry colormap
    QImage             image_;       // bins_ × depth_, circular by row
    std::vector<float> history_;     // raw dB, depth_ × bins_, same row layout
    int    bins_   = 0;
    int    depth_  = 0;
    int    topRow_ = 0;              // image row of the newest line
    int    rows_   = 0;              // valid lines stored so far (≤ depth_)
    double centerMHz_ = 0.0;         // frequency axis of the stored lines
    double spanMHz_   = 0.0;
    double visLoMHz_  = 0.0;         // visible range from the spectrum zoom
    double visHiMHz_  = 0.0;
    int    marginLeft_  = 0;         // pixel margins matching the spectrum
    int    marginRight_ = 0;         //   plot's axis rect (setEdgeMargins)
    QVector<Band> bands_;            // demodulator VFO bands (setFilterBands)
};
