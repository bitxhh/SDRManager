#include "RadioMonitorPage.h"

#include "CombinedRxController.h"
#include "DemodulatorPanel.h"
#include "FrequencyDial.h"
#include "RecordingSettingsDialog.h"
#include "WaterfallView.h"
#include "../Core/FileNaming.h"
#include "../Core/IDevice.h"
#include "../DSP/WaterfallHandler.h"
#include "../Hardware/DeviceController.h"
#include "qcustomplot.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QThreadPool>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
// Свернуть угол в (-180°, 180°] — та же семантика, что в IqCombiner.
double wrapTo180(double deg) {
    while (deg >  180.0) deg -= 360.0;
    while (deg <= -180.0) deg += 360.0;
    return deg;
}
} // namespace

// ---------------------------------------------------------------------------
RadioMonitorPage::RadioMonitorPage(IDevice*          device,
                                   DeviceController* controller,
                                   QThreadPool*      dspPool,
                                   QWidget*          parent)
    : QWidget(parent)
    , device_(device)
    , controller_(controller)
    , dspPool_(dspPool)
{
    ctrl_ = new CombinedRxController(device_, dspPool_, this);
    connect(ctrl_, &CombinedRxController::fftReady,
            this,  &RadioMonitorPage::onFftReady, Qt::QueuedConnection);
    connect(ctrl_, &CombinedRxController::streamStatus,
            this,  [this](const QString& msg) {
                if (statusLabel_) statusLabel_->setText(msg);
            }, Qt::QueuedConnection);
    connect(ctrl_, &CombinedRxController::streamError,
            this,  &RadioMonitorPage::onStreamErrorInternal, Qt::QueuedConnection);
    connect(ctrl_, &CombinedRxController::streamFinished,
            this,  &RadioMonitorPage::onStreamFinishedInternal, Qt::QueuedConnection);
    connect(ctrl_, &CombinedRxController::phaseMetric,
            this,  &RadioMonitorPage::onPhaseMetric);

    loadRecordingSettings();
    waterfallSettings_ = WaterfallSettings::load();
    waterfallHandler_  = new WaterfallHandler(this);
    buildUi();

    // Default channel selection: one RX0. DeviceDetailWindow overrides via
    // setActiveChannels() once the selection UI is known.
    activeChannels_.append({ChannelDescriptor::RX, 0});
    gainsDb_.append(0.0);
}

RadioMonitorPage::~RadioMonitorPage() {
    shutdown();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::buildUi() {
    auto* outer = new QVBoxLayout(this);

    auto* title = new QLabel("Радиомониторинг", this);
    title->setStyleSheet("font-weight: 600; font-size: 16px;");
    outer->addWidget(title);
    outer->addSpacing(4);

    // ── Frequency row ────────────────────────────────────────────────────────
    {
        auto* row  = new QWidget(this);
        auto* hlay = new QHBoxLayout(row);
        hlay->setContentsMargins(0, 0, 0, 0);

        auto* lbl = new QLabel("Center freq:", row);
        lbl->setFixedWidth(120);

        freqDial_ = new FrequencyDial(row);
        freqDial_->setRangeMHz(kFreqMinMHz, kFreqMaxMHz);
        freqDial_->setValueMHz(kFreqDefaultMHz);

        hlay->addWidget(lbl);
        hlay->addWidget(freqDial_);
        hlay->addStretch(1);
        outer->addWidget(row);

        connect(freqDial_, &FrequencyDial::valueCommitted, this, &RadioMonitorPage::applyFrequency);
    }

    // ── FFT plot ─────────────────────────────────────────────────────────────
    fftPlot_ = new QCustomPlot(this);
    fftPlot_->setMinimumHeight(240);
    setupFftPlot();
    outer->addWidget(fftPlot_, 2);

    // ── Waterfall (lines arrive from the DSP pool thread — Queued) ──────────
    waterfallView_ = new WaterfallView(this);
    waterfallView_->setMinimumHeight(120);
    outer->addWidget(waterfallView_, 1);
    connect(waterfallHandler_, &WaterfallHandler::lineReady,
            waterfallView_,    &WaterfallView::appendLine, Qt::QueuedConnection);
    // Клик/драг по водопаду — та же логика перестройки VFO, что и на спектре.
    connect(waterfallView_, &WaterfallView::freqPressed,
            this,           &RadioMonitorPage::handleTunePress);
    connect(waterfallView_, &WaterfallView::freqDragged,
            this,           &RadioMonitorPage::handleTuneDrag);
    connect(waterfallView_, &WaterfallView::freqReleased,
            this,           &RadioMonitorPage::endTuneDrag);
    connect(waterfallView_, &WaterfallView::freqHovered,
            this,           [this](double mhz) { updateHoverCursor(waterfallView_, mhz); });
    // Горизонтальное выравнивание со спектром: водопад рисует в тех же
    // пиксельных границах, что и axis rect графика (слева — поле оси Y).
    connect(fftPlot_, &QCustomPlot::afterReplot, this, [this] {
        const QRect r = fftPlot_->axisRect()->rect();
        waterfallView_->setEdgeMargins(r.x(), fftPlot_->width() - (r.x() + r.width()));
    });
    applyWaterfallSettings();

    // ── Controls row: + Add demod, Record, Settings ──────────────────────────
    {
        auto* row  = new QWidget(this);
        auto* hlay = new QHBoxLayout(row);
        hlay->setContentsMargins(0, 0, 0, 0);

        addDemodBtn_ = new QPushButton("+ Add demodulator", row);
        addDemodBtn_->setToolTip(QString("Up to %1 demodulators").arg(kMaxDemods));

        recordCheck_ = new QCheckBox("Record", row);
        recordCheck_->setToolTip(
            "Enable recording of raw/combined I/Q at the next stream start.\n"
            "Per-demodulator filtered/audio capture is controlled on each panel.");

        settingsBtn_ = new QPushButton("\u2699", row);
        settingsBtn_->setFixedWidth(32);
        settingsBtn_->setToolTip("Recording settings");

        waterfallBtn_ = new QPushButton("Waterfall", row);
        waterfallBtn_->setToolTip("Waterfall settings");

        hlay->addWidget(addDemodBtn_);
        hlay->addSpacing(12);
        hlay->addWidget(recordCheck_);
        hlay->addWidget(settingsBtn_);
        hlay->addSpacing(12);
        hlay->addWidget(waterfallBtn_);
        hlay->addStretch();
        outer->addWidget(row);

        connect(addDemodBtn_,  &QPushButton::clicked, this, &RadioMonitorPage::addDemodulator);
        connect(settingsBtn_,  &QPushButton::clicked, this, &RadioMonitorPage::openRecordingSettings);
        connect(waterfallBtn_, &QPushButton::clicked, this, &RadioMonitorPage::openWaterfallSettings);
    }

    // ── Фазовая синхронизация каналов (виден только при ≥2 RX) ──────────────
    {
        phaseRow_ = new QWidget(this);
        auto* hlay = new QHBoxLayout(phaseRow_);
        hlay->setContentsMargins(0, 0, 0, 0);

        auto* lbl = new QLabel("Phase ch0↔ch1:", phaseRow_);

        phaseMetricLabel_ = new QLabel("—", phaseRow_);
        phaseMetricLabel_->setStyleSheet("color: gray;");
        phaseMetricLabel_->setMinimumWidth(240);
        phaseMetricLabel_->setToolTip(
            "raw — сырая фаза ch0·conj(ch1); Δ — остаток после калибровки;\n"
            "coh — когерентность [0..1], осмысленна только на общем сигнале.");

        phaseCalLabel_ = new QLabel("cal 0.0°", phaseRow_);
        phaseCalLabel_->setStyleSheet("color: gray;");

        phaseCalBtn_ = new QPushButton("Calibrate", phaseRow_);
        phaseCalBtn_->setEnabled(false);   // требует живого 2-канального стрима
        phaseCalBtn_->setToolTip(
            "Снять текущую фазу как ноль и применить поворот к ch1.\n"
            "Оба канала должны принимать один сигнал (общая антенна/splitter).");

        phaseResetBtn_ = new QPushButton("Reset", phaseRow_);
        phaseResetBtn_->setToolTip("Сбросить фазовую калибровку в 0°.");

        phaseAutoCheck_ = new QCheckBox("Auto", phaseRow_);
        phaseAutoCheck_->setChecked(true);
        phaseAutoCheck_->setToolTip(
            "Автокалибровка: при coh ≥ 0.9 остаток Δ плавно сводится к нулю.\n"
            "Подходит для мониторинга (макс. SNR суммы). Для пеленгации/радара\n"
            "выключить — авто-режим уничтожает геометрическую фазу сигнала.");

        hlay->addWidget(lbl);
        hlay->addWidget(phaseMetricLabel_);
        hlay->addSpacing(12);
        hlay->addWidget(phaseCalLabel_);
        hlay->addWidget(phaseAutoCheck_);
        hlay->addWidget(phaseCalBtn_);
        hlay->addWidget(phaseResetBtn_);
        hlay->addStretch();
        outer->addWidget(phaseRow_);
        phaseRow_->setVisible(false);   // setActiveChannels() включит при 2×RX

        connect(phaseCalBtn_,   &QPushButton::clicked, this, &RadioMonitorPage::calibratePhase);
        connect(phaseResetBtn_, &QPushButton::clicked, this, &RadioMonitorPage::resetPhaseCalibration);
    }

    // ── Demodulator panels area (scrollable) ─────────────────────────────────
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMinimumHeight(140);

        auto* host = new QWidget(scroll);
        panelsLayout_ = new QVBoxLayout(host);
        panelsLayout_->setContentsMargins(0, 0, 0, 0);
        panelsLayout_->setSpacing(4);
        panelsLayout_->addStretch();
        scroll->setWidget(host);
        outer->addWidget(scroll);
    }

    // ── Stream start / stop ──────────────────────────────────────────────────
    {
        auto* row  = new QWidget(this);
        auto* hlay = new QHBoxLayout(row);
        hlay->setContentsMargins(0, 0, 0, 0);

        startBtn_ = new QPushButton("\u25B6  Start", row);
        stopBtn_  = new QPushButton("\u25A0  Stop",  row);
        startBtn_->setEnabled(controller_->isInitialized());
        stopBtn_->setEnabled(false);

        connect(startBtn_, &QPushButton::clicked, this, &RadioMonitorPage::startStream);
        connect(stopBtn_,  &QPushButton::clicked, this, &RadioMonitorPage::stopStream);

        statusLabel_ = new QLabel("Idle", row);
        statusLabel_->setStyleSheet("color: gray;");

        hlay->addWidget(startBtn_);
        hlay->addWidget(stopBtn_);
        hlay->addSpacing(12);
        hlay->addWidget(statusLabel_);
        hlay->addStretch();
        outer->addWidget(row);
    }
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::setupFftPlot() {
    fftPlot_->addGraph();
    fftPlot_->graph(0)->setPen(QPen(QColor(0, 200, 255), 1.2));

    fftPlot_->xAxis->setLabel("Frequency (MHz)");
    fftPlot_->yAxis->setLabel("Power (dB)");
    fftPlot_->yAxis->setRange(-120, 0);

    fftPlot_->setBackground(QBrush(QColor(30, 30, 30)));
    fftPlot_->xAxis->setBasePen(QPen(Qt::white));
    fftPlot_->yAxis->setBasePen(QPen(Qt::white));
    fftPlot_->xAxis->setTickPen(QPen(Qt::white));
    fftPlot_->yAxis->setTickPen(QPen(Qt::white));
    fftPlot_->xAxis->setSubTickPen(QPen(Qt::gray));
    fftPlot_->yAxis->setSubTickPen(QPen(Qt::gray));
    fftPlot_->xAxis->setTickLabelColor(Qt::white);
    fftPlot_->yAxis->setTickLabelColor(Qt::white);
    fftPlot_->xAxis->setLabelColor(Qt::white);
    fftPlot_->yAxis->setLabelColor(Qt::white);
    fftPlot_->setInteractions(QCP::iRangeZoom);
    fftPlot_->axisRect()->setRangeZoom(Qt::Horizontal);

    // Center frequency marker (red dashed).
    centerLine_ = new QCPItemLine(fftPlot_);
    centerLine_->setPen(QPen(QColor(255, 60, 60), 1.2, Qt::DashLine));
    centerLine_->setAntialiased(false);
    centerLine_->start->setCoords(kFreqDefaultMHz, -130.0);
    centerLine_->end->setCoords  (kFreqDefaultMHz,   10.0);
    centerLine_->start->setType(QCPItemPosition::ptPlotCoords);
    centerLine_->end->setType  (QCPItemPosition::ptPlotCoords);

    // X-axis zoom clamp.
    connect(fftPlot_->xAxis, qOverload<const QCPRange&>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& newRange) {
        if (fftPlot_->graph(0)->dataCount() < 2) return;
        const double lo   = fftPlot_->graph(0)->data()->begin()->key;
        const double hi   = (fftPlot_->graph(0)->data()->end() - 1)->key;
        const double span = hi - lo;
        if (newRange.size() > span * 1.01) {
            QSignalBlocker b(fftPlot_->xAxis);
            fftPlot_->xAxis->setRange(lo, hi);
            plotUserZoomed_ = false;
        } else {
            plotUserZoomed_ = true;
        }
        if (waterfallView_) {
            const QCPRange r = fftPlot_->xAxis->range();
            waterfallView_->setVisibleFreqRange(r.lower, r.upper);
        }
    });

    // Y-axis clamp.
    connect(fftPlot_->yAxis, qOverload<const QCPRange&>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& newRange) {
        constexpr double yMin = -130.0, yMax = 10.0;
        if (newRange.lower < yMin || newRange.upper > yMax) {
            QSignalBlocker b(fftPlot_->yAxis);
            fftPlot_->yAxis->setRange(std::max(newRange.lower, yMin),
                                      std::min(newRange.upper, yMax));
        }
    });

    // Double-click: reset zoom.
    connect(fftPlot_, &QCustomPlot::mouseDoubleClick, this, [this](QMouseEvent*) {
        plotUserZoomed_ = false;
        if (fftPlot_->graph(0)->dataCount() >= 2) {
            QSignalBlocker b(fftPlot_->xAxis);
            fftPlot_->xAxis->setRange(
                fftPlot_->graph(0)->data()->begin()->key,
                (fftPlot_->graph(0)->data()->end() - 1)->key);
        }
        fftPlot_->yAxis->setRange(-120.0, 0.0);
        if (waterfallView_) {
            const QCPRange r = fftPlot_->xAxis->range();
            waterfallView_->setVisibleFreqRange(r.lower, r.upper);
        }
        fftPlot_->replot(QCustomPlot::rpQueuedReplot);
    });

    // ЛКМ на спектре: край полосы — ширина фильтра, внутри полосы — драг VFO
    // этого демода, вне полос — перестройка первого активного демода
    // (см. handleTunePress).
    connect(fftPlot_, &QCustomPlot::mousePress, this, [this](QMouseEvent* event) {
        if (event->button() != Qt::LeftButton) return;
        handleTunePress(fftPlot_->xAxis->pixelToCoord(event->pos().x()));
    });
    connect(fftPlot_, &QCustomPlot::mouseMove, this, [this](QMouseEvent* event) {
        const double mhz = fftPlot_->xAxis->pixelToCoord(event->pos().x());
        if (event->buttons() & Qt::LeftButton) handleTuneDrag(mhz);
        else                                   updateHoverCursor(fftPlot_, mhz);
    });
    connect(fftPlot_, &QCustomPlot::mouseRelease, this, [this](QMouseEvent* event) {
        if (event->button() == Qt::LeftButton) endTuneDrag();
    });
}

// ---------------------------------------------------------------------------
// Клик/драг перестройки VFO — общая логика для спектра и водопада.
// ---------------------------------------------------------------------------
double RadioMonitorPage::hitToleranceMHz() const {
    // Узкие полосы (SSB/CW) почти невозможно поймать точно — гарантируем
    // зону захвата ~4 px в текущем масштабе оси X.
    if (!fftPlot_ || fftPlot_->axisRect()->width() <= 0) return 0.0;
    return fftPlot_->xAxis->range().size() * 4.0 / fftPlot_->axisRect()->width();
}

int RadioMonitorPage::demodIndexAtFreq(double mhz) const {
    const double tol = hitToleranceMHz();
    for (int i = 0; i < panels_.size(); ++i) {
        auto* p = panels_[i];
        if (p->currentMode().isEmpty()) continue;
        // Полоса на графике рисуется как vfo ± bwMHz (см. updateFilterBands).
        const double half = std::max(p->currentBwMHz(), tol);
        if (std::abs(mhz - p->vfoFreqMHz()) <= half) return i;
    }
    return -1;
}

int RadioMonitorPage::demodEdgeIndexAtFreq(double mhz) const {
    const double tol = hitToleranceMHz();
    for (int i = 0; i < panels_.size(); ++i) {
        auto* p = panels_[i];
        if (p->currentMode().isEmpty()) continue;
        const double bw = p->currentBwMHz();
        if (bw <= 0.0) continue;
        // Зона края не шире трети полосы — у узких фильтров центр
        // должен оставаться доступным для сдвига.
        const double edgeTol = std::min(tol, bw / 3.0);
        if (std::abs(std::abs(mhz - p->vfoFreqMHz()) - bw) <= edgeTol) return i;
    }
    return -1;
}

int RadioMonitorPage::firstActiveDemodIndex() const {
    for (int i = 0; i < panels_.size(); ++i)
        if (!panels_[i]->currentMode().isEmpty()) return i;
    return -1;
}

void RadioMonitorPage::handleTunePress(double mhz) {
    int idx = demodEdgeIndexAtFreq(mhz);
    if (idx >= 0) {
        // Захват края: ширина = |курсор − VFO| минус промах мимо края при нажатии.
        auto* p = panels_[idx];
        dragPanelIndex_    = idx;
        dragResize_        = true;
        dragGrabOffsetMHz_ = std::abs(mhz - p->vfoFreqMHz()) - p->currentBwMHz();
        return;
    }
    dragResize_ = false;
    idx = demodIndexAtFreq(mhz);
    if (idx >= 0) {
        // Захват полосы: VFO следует за курсором с сохранением точки хвата.
        dragPanelIndex_    = idx;
        dragGrabOffsetMHz_ = mhz - panels_[idx]->vfoFreqMHz();
        return;
    }
    idx = firstActiveDemodIndex();
    if (idx < 0) return;
    panels_[idx]->tuneToMHz(mhz);
    // Пока ЛКМ зажата, тот же демод продолжает следовать за курсором.
    dragPanelIndex_    = idx;
    dragGrabOffsetMHz_ = 0.0;
}

void RadioMonitorPage::handleTuneDrag(double mhz) {
    if (dragPanelIndex_ < 0 || dragPanelIndex_ >= panels_.size()) return;
    auto* p = panels_[dragPanelIndex_];
    if (dragResize_) {
        // Спинбокс Bandwidth сам ограничит значение диапазоном модема.
        const double bwMHz = std::abs(mhz - p->vfoFreqMHz()) - dragGrabOffsetMHz_;
        p->setBandwidthHz(std::max(bwMHz, 0.0) * 1e6);
        return;
    }
    p->tuneToMHz(mhz - dragGrabOffsetMHz_);
}

void RadioMonitorPage::endTuneDrag() {
    dragPanelIndex_ = -1;
    dragResize_     = false;
}

void RadioMonitorPage::updateHoverCursor(QWidget* w, double mhz) {
    if (!w) return;
    if      (demodEdgeIndexAtFreq(mhz) >= 0) w->setCursor(Qt::SizeHorCursor);
    else if (demodIndexAtFreq(mhz) >= 0)     w->setCursor(Qt::OpenHandCursor);
    else                                     w->unsetCursor();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::setActiveChannels(const QList<ChannelDescriptor>& channels) {
    activeChannels_ = channels;
    // Make gain vector match channel count if the caller hasn't supplied one yet.
    if (gainsDb_.size() != channels.size())
        gainsDb_.resize(channels.size());
    // Фазовая синхронизация имеет смысл только для когерентной пары каналов.
    if (phaseRow_) phaseRow_->setVisible(channels.size() >= 2);
}

void RadioMonitorPage::setChannelGains(const QVector<double>& gainsDb) {
    gainsDb_ = gainsDb;
    if (ctrl_ && ctrl_->isStreaming()) {
        for (int i = 0; i < gainsDb_.size(); ++i)
            ctrl_->setChannelGain(i, gainsDb_[i]);
    }
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::onDeviceReady() {
    if (startBtn_) startBtn_->setEnabled(true);
    const double sr = device_ ? device_->sampleRate() : 0.0;
    for (auto* p : panels_) p->setSampleRateHz(sr);
}

// ---------------------------------------------------------------------------
bool RadioMonitorPage::isStreaming() const {
    return ctrl_ && ctrl_->isStreaming();
}

double RadioMonitorPage::centerFreqMHz() const {
    return freqDial_ ? freqDial_->valueMHz() : kFreqDefaultMHz;
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::applyFrequency() {
    if (!controller_->isInitialized()) return;
    const double mhz = freqDial_->valueMHz();

    // Push to all active RX channels (LimeSDR shares one PLL but we still
    // broadcast in case the device supports independent LO per channel).
    for (const auto& ch : activeChannels_)
        controller_->setFrequencyChannel(ch, mhz);

    if (ctrl_) ctrl_->setFftCenterFreq(mhz);
    if (waterfallHandler_) waterfallHandler_->setCenterFrequency(mhz);
    for (auto* p : panels_) p->setCenterFreqMHz(mhz);

    if (centerLine_) {
        centerLine_->start->setCoords(mhz, -130.0);
        centerLine_->end->setCoords  (mhz,   10.0);
        fftPlot_->replot(QCustomPlot::rpQueuedReplot);
    }
    updateFilterBands();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::startStream() {
    if (isStreaming()) return;
    if (activeChannels_.isEmpty()) {
        QMessageBox::warning(this, "Stream", "No active RX channels selected.");
        return;
    }

    CombinedRxController::StreamConfig cfg;
    cfg.loFreqMHz = centerFreqMHz();
    cfg.channels  = activeChannels_;
    cfg.gainsDb.clear();
    for (int i = 0; i < activeChannels_.size(); ++i) {
        const double g = (i < gainsDb_.size()) ? gainsDb_[i] : 0.0;
        cfg.gainsDb.append(g);
    }

    const double sr        = device_ ? device_->sampleRate() : 0.0;
    const double centerHz  = cfg.loFreqMHz * 1e6;
    sessionTimestamp_          = FileNaming::currentTimestamp();
    const QString& timestamp   = sessionTimestamp_;
    const QString combinedSrc  = FileNaming::combinedSource(activeChannels_);

    // ── Raw/Combined recording paths ────────────────────────────────────────
    if (recordCheck_->isChecked() && !recordingSettings_.outputDir.isEmpty()) {
        QDir().mkpath(recordingSettings_.outputDir);
        cfg.rawFormat = recordingSettings_.rawFormat;
        const QString ext = recordingSettings_.rawExtension();

        if (recordingSettings_.recordRawPerChannel) {
            for (const auto& ch : activeChannels_) {
                cfg.rawPerChannelPaths.append(FileNaming::compose(
                    recordingSettings_.outputDir, timestamp,
                    FileNaming::perChannelSource(ch),
                    centerHz, sr, ext));
            }
        }
        if (recordingSettings_.recordCombined) {
            cfg.recordRaw = true;
            cfg.rawPath = FileNaming::compose(
                recordingSettings_.outputDir, timestamp,
                combinedSrc, centerHz, sr, ext);
        }
    }

    // Hand the session-wide context to every panel — each decides whether to
    // activate its own filtered/audio writers based on its local checkboxes.
    pushRecordingContextToPanels(timestamp, combinedSrc, centerHz);

    for (auto* p : panels_) {
        p->setCenterFreqMHz(cfg.loFreqMHz);
        p->setSampleRateHz(sr);
    }

    ctrl_->startStream(cfg);

    // IqCombiner пересоздаётся на каждый старт — заново применяем сохранённую
    // фазовую калибровку.
    if (phaseCalDeg_ != 0.0)
        ctrl_->setPhaseCalibrationDeg(phaseCalDeg_);
    if (phaseCalBtn_) phaseCalBtn_->setEnabled(activeChannels_.size() >= 2);

    // Re-attach panel demodulators now that combinedPipeline_ is live.
    for (auto* p : panels_) p->onStreamStarted();

    // Водопад: combinedPipeline_ пересоздаётся на каждый старт — цепляем заново.
    waterfallHandler_->setCenterFrequency(cfg.loFreqMHz);
    ctrl_->addExtraHandler(waterfallHandler_);

    startBtn_->setEnabled(false);
    stopBtn_->setEnabled(true);
    statusLabel_->setStyleSheet("color: #00cc44;");
    statusLabel_->setText("Streaming\u2026");

    emit streamStarted();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::stopStream() {
    if (!ctrl_) return;
    ctrl_->stopStream();
    stopBtn_->setEnabled(false);
}

void RadioMonitorPage::shutdown() {
    if (!ctrl_) return;
    for (auto* p : panels_) p->onStreamStopped();
    ctrl_->shutdown();
}

void RadioMonitorPage::stopStreamSync() {
    if (!isStreaming()) return;
    shutdown();
    // teardownStream() disconnects the workers' finished() signal, so
    // streamFinished never arrives — without this Start stays disabled.
    onStreamFinishedInternal();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::onStreamErrorInternal(const QString& err) {
    if (statusLabel_) {
        statusLabel_->setStyleSheet("color: #ff4444;");
        statusLabel_->setText("Error: " + err);
    }
    emit errorOccurred(err);
}

void RadioMonitorPage::onStreamFinishedInternal() {
    for (auto* p : panels_) p->onStreamStopped();

    if (startBtn_) startBtn_->setEnabled(controller_->isInitialized());
    if (stopBtn_)  stopBtn_->setEnabled(false);
    if (statusLabel_) {
        statusLabel_->setStyleSheet("color: gray;");
        statusLabel_->setText("Idle");
    }
    if (phaseCalBtn_) phaseCalBtn_->setEnabled(false);
    if (phaseMetricLabel_) {
        phaseMetricLabel_->setText("—");
        phaseMetricLabel_->setStyleSheet("color: gray;");
    }
    emit streamStopped();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::setPhaseCalibrationDeg(double deg) {
    phaseCalDeg_ = deg;
    if (ctrl_) ctrl_->setPhaseCalibrationDeg(deg);   // no-op без живого комбайнера
    if (phaseCalLabel_)
        phaseCalLabel_->setText(QString("cal %1°").arg(deg, 0, 'f', 1));
}

void RadioMonitorPage::onPhaseMetric(double rawDeg, double calibratedDeg, double coherence) {
    // Автокалибровка: плавно сводим остаток Δ к нулю, пока когерентность
    // высокая. α = 0.5 при эмиссии метрики 5 Гц сводит 90° к <1° за ~1.5 с;
    // мёртвая зона 1° не даёт дёргать калибровку на шуме оценки.
    if (phaseAutoCheck_ && phaseAutoCheck_->isChecked()
        && coherence >= 0.90 && std::abs(calibratedDeg) > 1.0) {
        setPhaseCalibrationDeg(wrapTo180(phaseCalDeg_ + 0.5 * calibratedDeg));
    }

    if (!phaseMetricLabel_) return;
    phaseMetricLabel_->setText(QString("raw %1°   Δ %2°   coh %3")
        .arg(rawDeg,        0, 'f', 1)
        .arg(calibratedDeg, 0, 'f', 1)
        .arg(coherence,     0, 'f', 2));
    const char* color = coherence > 0.9 ? "#00cc44"
                      : coherence > 0.5 ? "#ffaa00"
                                        : "gray";
    phaseMetricLabel_->setStyleSheet(QString("color: %1;").arg(color));
}

void RadioMonitorPage::calibratePhase() {
    if (!ctrl_ || !ctrl_->isStreaming()) return;
    phaseCalDeg_ = ctrl_->calibratePhase();
    if (phaseCalLabel_)
        phaseCalLabel_->setText(QString("cal %1°").arg(phaseCalDeg_, 0, 'f', 1));
}

void RadioMonitorPage::resetPhaseCalibration() {
    setPhaseCalibrationDeg(0.0);
}

void RadioMonitorPage::setPhaseAutoCal(bool on) {
    if (phaseAutoCheck_) phaseAutoCheck_->setChecked(on);
}

bool RadioMonitorPage::phaseAutoCal() const {
    return phaseAutoCheck_ && phaseAutoCheck_->isChecked();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::addDemodulator() {
    if (panels_.size() >= kMaxDemods) {
        QMessageBox::information(this, "Demodulators",
            QString("Maximum %1 demodulators allowed.").arg(kMaxDemods));
        return;
    }

    const int slot = panels_.size();
    auto* panel = new DemodulatorPanel(slot, this);

    panel->attachToController(ctrl_);
    panel->setCenterFreqMHz(centerFreqMHz());
    panel->setSampleRateHz(device_ ? device_->sampleRate() : 0.0);

    connect(panel, &DemodulatorPanel::removeRequested,
            this,  &RadioMonitorPage::removeDemodulator);
    connect(panel, &DemodulatorPanel::vfoChanged,
            this,  [this](int, double, double) { updateFilterBands(); });

    // Insert before the stretch item at the end of panelsLayout_.
    const int insertAt = panelsLayout_->count() - 1;
    panelsLayout_->insertWidget(insertAt, panel);
    panels_.append(panel);

    // Companion green VFO band overlay on the spectrum.
    auto* band = new QCPItemRect(fftPlot_);
    band->setBrush(QBrush(QColor(0, 200, 80, 40)));
    band->setPen(QPen(QColor(0, 200, 80, 120), 1.0));
    band->topLeft->setType(QCPItemPosition::ptPlotCoords);
    band->bottomRight->setType(QCPItemPosition::ptPlotCoords);
    band->setVisible(false);
    vfoBands_.append(band);

    // If stream is already running, attach immediately and ensure the panel
    // has the current session's recording context so its checkboxes work.
    if (isStreaming()) {
        const double centerHz     = centerFreqMHz() * 1e6;
        const QString combinedSrc = FileNaming::combinedSource(activeChannels_);
        const bool filteredAllowed =
            recordCheck_->isChecked() && recordingSettings_.recordFiltered
            && !recordingSettings_.outputDir.isEmpty();
        const bool audioAllowed =
            recordCheck_->isChecked() && recordingSettings_.recordAudio
            && !recordingSettings_.outputDir.isEmpty();
        panel->setRecordingContext(recordingSettings_.outputDir,
                                   sessionTimestamp_, combinedSrc, centerHz,
                                   filteredAllowed, audioAllowed);
        panel->onStreamStarted();
    }

    updateFilterBands();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::removeDemodulator(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= panels_.size()) return;

    endTuneDrag();   // индексы panels_ сдвигаются — активный драг недействителен

    DemodulatorPanel* panel = panels_.takeAt(slotIndex);
    panel->detachFromController();
    panelsLayout_->removeWidget(panel);
    panel->deleteLater();

    if (slotIndex < vfoBands_.size()) {
        QCPItemRect* band = vfoBands_.takeAt(slotIndex);
        fftPlot_->removeItem(band);
    }

    // No renumbering — slot index stays stable per panel instance.
    updateFilterBands();
    fftPlot_->replot(QCustomPlot::rpQueuedReplot);
}

// ---------------------------------------------------------------------------
// Persistence — collect/restore DemodulatorPanel state.
// ---------------------------------------------------------------------------
QList<DemodPanelSettings> RadioMonitorPage::demodPanelStates() const {
    QList<DemodPanelSettings> out;
    out.reserve(panels_.size());
    for (const auto* p : panels_) out.append(p->state());
    return out;
}

void RadioMonitorPage::restoreDemodPanels(const QList<DemodPanelSettings>& panels) {
    // Called once at window construction, before any stream is started.
    // Skip if anything was already added manually.
    if (!panels_.isEmpty()) return;

    const int n = std::min(static_cast<int>(panels.size()), kMaxDemods);
    for (int i = 0; i < n; ++i) {
        addDemodulator();
        if (i < panels_.size()) panels_[i]->applyState(panels[i]);
    }
    updateFilterBands();
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::updateFilterBands() {
    if (!fftPlot_) return;

    QVector<WaterfallView::Band> wfBands;
    for (int i = 0; i < panels_.size() && i < vfoBands_.size(); ++i) {
        auto* panel = panels_[i];
        auto* band  = vfoBands_[i];

        const QString mode = panel->currentMode();
        if (mode.isEmpty()) { band->setVisible(false); continue; }

        const double bwMHz = panel->currentBwMHz();
        const double vfo   = panel->vfoFreqMHz();
        band->topLeft->setCoords    (vfo - bwMHz, 10.0);
        band->bottomRight->setCoords(vfo + bwMHz, -130.0);
        band->setVisible(true);
        wfBands.append({vfo - bwMHz, vfo + bwMHz});
    }
    if (waterfallView_) waterfallView_->setFilterBands(wfBands);
    fftPlot_->replot(QCustomPlot::rpQueuedReplot);
}

// ---------------------------------------------------------------------------
void RadioMonitorPage::onFftReady(FftFrame frame) {
    fftPlot_->graph(0)->setData(frame.freqMHz, frame.powerDb);

    if (centerLine_) {
        const double mhz = centerFreqMHz();
        centerLine_->start->setCoords(mhz, -130.0);
        centerLine_->end->setCoords  (mhz,   10.0);
    }

    if (!plotUserZoomed_ && !frame.freqMHz.isEmpty()) {
        QSignalBlocker b(fftPlot_->xAxis);
        fftPlot_->xAxis->setRange(frame.freqMHz.first(), frame.freqMHz.last());
    }

    // QSignalBlocker suppresses rangeChanged above — push the visible range
    // to the waterfall here (setVisibleFreqRange early-outs when unchanged).
    if (waterfallView_) {
        const QCPRange r = fftPlot_->xAxis->range();
        waterfallView_->setVisibleFreqRange(r.lower, r.upper);
    }

    fftDirty_ = true;
}

void RadioMonitorPage::replotIfDirty() {
    if (!fftDirty_ || !fftPlot_->isVisible()) return;
    fftDirty_ = false;
    fftPlot_->replot(QCustomPlot::rpQueuedReplot);
}

// ---------------------------------------------------------------------------
// Recording settings
// ---------------------------------------------------------------------------
void RadioMonitorPage::openRecordingSettings() {
    RecordingSettingsDialog dlg(recordingSettings_, this);
    if (dlg.exec() != QDialog::Accepted) return;

    recordingSettings_ = dlg.settings();
    saveRecordingSettings();

    // If the stream is live, re-broadcast the context so panels can react to
    // freshly enabled/disabled filtered/audio toggles.
    if (ctrl_ && ctrl_->isStreaming()) {
        const double centerHz     = centerFreqMHz() * 1e6;
        const QString combinedSrc = FileNaming::combinedSource(activeChannels_);
        pushRecordingContextToPanels(sessionTimestamp_, combinedSrc, centerHz);
    }
}

void RadioMonitorPage::pushRecordingContextToPanels(const QString& timestamp,
                                                    const QString& combinedSource,
                                                    double         centerFreqHz)
{
    const bool filteredAllowed =
        recordCheck_->isChecked() && recordingSettings_.recordFiltered
        && !recordingSettings_.outputDir.isEmpty();
    const bool audioAllowed =
        recordCheck_->isChecked() && recordingSettings_.recordAudio
        && !recordingSettings_.outputDir.isEmpty();

    if ((filteredAllowed || audioAllowed) && !recordingSettings_.outputDir.isEmpty())
        QDir().mkpath(recordingSettings_.outputDir);

    for (auto* p : panels_) {
        p->setRecordingContext(recordingSettings_.outputDir, timestamp,
                               combinedSource, centerFreqHz,
                               filteredAllowed, audioAllowed);
    }
}

void RadioMonitorPage::loadRecordingSettings() {
    // Explicit org/app — default QSettings is a no-op without setOrganizationName().
    QSettings s(QStringLiteral("SDRManager"), QStringLiteral("SDRManager"));
    const QString defaultDir = QDir(QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation)).filePath("stand_recordings");
    recordingSettings_.outputDir =
        s.value("recording/outputDir", defaultDir).toString();
    recordingSettings_.recordRawPerChannel =
        s.value("recording/rawPerChannel", false).toBool();
    recordingSettings_.recordCombined =
        s.value("recording/combined", false).toBool();
    recordingSettings_.recordFiltered =
        s.value("recording/filtered", false).toBool();
    recordingSettings_.recordAudio =
        s.value("recording/audio", false).toBool();
    recordingSettings_.rawFormat = static_cast<RecordingSettings::RawFormat>(
        s.value("recording/rawFormat",
                static_cast<int>(RecordingSettings::RawFormat::Float32)).toInt());
}

void RadioMonitorPage::saveRecordingSettings() const {
    QSettings s(QStringLiteral("SDRManager"), QStringLiteral("SDRManager"));
    s.setValue("recording/outputDir",     recordingSettings_.outputDir);
    s.setValue("recording/rawPerChannel", recordingSettings_.recordRawPerChannel);
    s.setValue("recording/combined",      recordingSettings_.recordCombined);
    s.setValue("recording/filtered",      recordingSettings_.recordFiltered);
    s.setValue("recording/audio",         recordingSettings_.recordAudio);
    s.setValue("recording/rawFormat",     static_cast<int>(recordingSettings_.rawFormat));
}

// ---------------------------------------------------------------------------
// Waterfall
// ---------------------------------------------------------------------------
void RadioMonitorPage::openWaterfallSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle("Waterfall settings");
    auto* form = new QFormLayout(&dlg);

    auto* enabledCheck = new QCheckBox(&dlg);
    enabledCheck->setChecked(waterfallSettings_.enabled);
    form->addRow("Enabled", enabledCheck);

    auto* colormapBox = new QComboBox(&dlg);
    colormapBox->addItems({"Classic SDR", "Viridis", "Inferno", "Turbo", "Grayscale"});
    colormapBox->setCurrentIndex(int(waterfallSettings_.colormap));
    form->addRow("Colormap", colormapBox);

    auto* aggBox = new QComboBox(&dlg);
    aggBox->addItems({"Max hold", "Average"});
    aggBox->setCurrentIndex(int(waterfallSettings_.aggregation));
    form->addRow("Aggregation", aggBox);

    auto* fftBox = new QComboBox(&dlg);
    for (int n = 512; n <= 16384; n *= 2)
        fftBox->addItem(QString::number(n), n);
    fftBox->setCurrentIndex(std::max(0, fftBox->findData(waterfallSettings_.fftSize)));
    form->addRow("FFT size", fftBox);

    auto* fpsSpin = new QSpinBox(&dlg);
    fpsSpin->setRange(5, 60);
    fpsSpin->setValue(waterfallSettings_.fps);
    form->addRow("Lines per second", fpsSpin);

    auto* depthSpin = new QSpinBox(&dlg);
    depthSpin->setRange(200, 4000);
    depthSpin->setSingleStep(100);
    depthSpin->setSuffix(" lines");
    depthSpin->setValue(waterfallSettings_.historyDepth);
    depthSpin->setToolTip("Сколько строк водопада хранится в памяти.\n"
                          "Изменение глубины очищает текущую историю.");
    form->addRow("History depth", depthSpin);

    auto* dbMinSpin = new QSpinBox(&dlg);
    dbMinSpin->setRange(-160, -20);
    dbMinSpin->setSuffix(" dB");
    dbMinSpin->setValue(int(waterfallSettings_.dbMin));
    form->addRow("dB min", dbMinSpin);

    auto* dbMaxSpin = new QSpinBox(&dlg);
    dbMaxSpin->setRange(-120, 20);
    dbMaxSpin->setSuffix(" dB");
    dbMaxSpin->setValue(int(waterfallSettings_.dbMax));
    form->addRow("dB max", dbMaxSpin);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    waterfallSettings_.enabled     = enabledCheck->isChecked();
    waterfallSettings_.colormap    = WaterfallSettings::Colormap(colormapBox->currentIndex());
    waterfallSettings_.aggregation = WaterfallSettings::Aggregation(aggBox->currentIndex());
    waterfallSettings_.fftSize     = fftBox->currentData().toInt();
    waterfallSettings_.fps          = fpsSpin->value();
    waterfallSettings_.historyDepth = depthSpin->value();
    waterfallSettings_.dbMin       = dbMinSpin->value();
    waterfallSettings_.dbMax       = dbMaxSpin->value();
    if (waterfallSettings_.dbMax - waterfallSettings_.dbMin < 5.0)
        waterfallSettings_.dbMax = waterfallSettings_.dbMin + 5.0;

    waterfallSettings_.save();
    applyWaterfallSettings();
}

void RadioMonitorPage::applyWaterfallSettings() {
    if (waterfallHandler_) {
        waterfallHandler_->setEnabled(waterfallSettings_.enabled);
        waterfallHandler_->setFftSize(waterfallSettings_.fftSize);
        waterfallHandler_->setFps(waterfallSettings_.fps);
        waterfallHandler_->setAggregation(waterfallSettings_.aggregation);
    }
    if (waterfallView_) {
        waterfallView_->applySettings(waterfallSettings_);
        waterfallView_->setVisible(waterfallSettings_.enabled);
    }
}
