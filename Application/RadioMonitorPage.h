#pragma once

#include "../Core/ChannelDescriptor.h"
#include "../Core/DeviceSettings.h"
#include "../Core/RecordingSettings.h"
#include "../Core/WaterfallSettings.h"
#include "../DSP/FftProcessor.h"

#include <QWidget>
#include <QList>
#include <QVector>

class QCustomPlot;
class QCPItemLine;
class QCPItemRect;
class QDoubleSpinBox;
class QSlider;
class QPushButton;
class QLabel;
class QCheckBox;
class QVBoxLayout;
class QThreadPool;

class IDevice;
class DeviceController;
class CombinedRxController;
class DemodulatorPanel;
class FrequencyDial;
class WaterfallHandler;
class WaterfallView;

// ---------------------------------------------------------------------------
// RadioMonitorPage — единая вкладка радиомониторинга.
//
// Layout:
//   [ Freq dial ]
//   [ FFT plot (single spectrum, combined I/Q) ]
//   [ + Add demodulator ] [ Record ] [ Settings ]
//   [ DemodulatorPanel 1 … DemodulatorPanel N ]  (макс 4)
//   [ Start / Stop ] [ Status ]
//
// Owns CombinedRxController. The page is responsible for wiring
// CombinedRxController → QCustomPlot (single FFT), and for creating /
// destroying DemodulatorPanel instances that each hold their own demod
// handler and audio output attached via ctrl->addExtraHandler.
// ---------------------------------------------------------------------------
class RadioMonitorPage : public QWidget {
    Q_OBJECT

public:
    RadioMonitorPage(IDevice*          device,
                     DeviceController* controller,
                     QThreadPool*      dspPool,
                     QWidget*          parent = nullptr);
    ~RadioMonitorPage() override;

    void setActiveChannels(const QList<ChannelDescriptor>& channels);
    void setChannelGains(const QVector<double>& gainsDb);

    // DeviceDetailWindow drives this from its plot timer.
    void replotIfDirty();

    // Called by DeviceDetailWindow when the device finishes initialization,
    // so buttons can be enabled and the VFO ranges can be set from SR.
    void onDeviceReady();

    // Synchronous stream teardown — blocks until workers exit. Does NOT reset
    // the UI or emit streamStopped (used on window close).
    void shutdown();

    // Synchronous stop for a live session (e.g. before a sample-rate change):
    // same as shutdown(), plus the UI reset + streamStopped that the async
    // path gets from CombinedRxController::streamFinished.
    void stopStreamSync();

    [[nodiscard]] bool isStreaming() const;
    [[nodiscard]] double centerFreqMHz() const;

    // Persistence — DemodPanel slot states.
    [[nodiscard]] QList<DemodPanelSettings> demodPanelStates() const;
    void restoreDemodPanels(const QList<DemodPanelSettings>& panels);

    // Persistence — межканальная фазовая калибровка (deg). Значение живёт
    // здесь, т.к. IqCombiner пересоздаётся на каждый startStream; страница
    // пушит его в контроллер после каждого запуска стрима.
    void setPhaseCalibrationDeg(double deg);
    [[nodiscard]] double phaseCalibrationDeg() const { return phaseCalDeg_; }

    // Persistence — автокалибровка фазы (чекбокс "Auto" в фазовой строке).
    void setPhaseAutoCal(bool on);
    [[nodiscard]] bool phaseAutoCal() const;

signals:
    void streamStarted();
    void streamStopped();
    void errorOccurred(const QString& message);

private slots:
    void onFftReady(FftFrame frame);
    void applyFrequency();
    void startStream();
    void stopStream();
    void onStreamFinishedInternal();
    void onStreamErrorInternal(const QString& err);

    void addDemodulator();
    void removeDemodulator(int slotIndex);
    void openRecordingSettings();
    void openWaterfallSettings();

    void onPhaseMetric(double rawDeg, double calibratedDeg, double coherence);
    void calibratePhase();
    void resetPhaseCalibration();

private:
    void buildUi();
    void setupFftPlot();
    void updateFilterBands();

    // ── Клик/драг перестройки VFO (общая логика спектра и водопада) ─────────
    // Нажатие на край полосы фильтра → изменение ширины (симметрично от VFO);
    // внутри полосы → драг VFO этого демода; вне полос — перестройка первого
    // активного демода (и драг его же, пока ЛКМ зажата).
    void handleTunePress(double mhz);
    void handleTuneDrag(double mhz);
    void endTuneDrag();
    void updateHoverCursor(QWidget* w, double mhz);
    [[nodiscard]] int    demodEdgeIndexAtFreq(double mhz) const;   // -1 = не на краю
    [[nodiscard]] int    demodIndexAtFreq(double mhz) const;
    [[nodiscard]] int    firstActiveDemodIndex() const;
    [[nodiscard]] double hitToleranceMHz() const;   // мин. половина зоны захвата (≈4 px)
    void pushRecordingContextToPanels(const QString& timestamp,
                                      const QString& combinedSource,
                                      double         centerFreqHz);
    void loadRecordingSettings();
    void saveRecordingSettings() const;
    void applyWaterfallSettings();

    IDevice*          device_;
    DeviceController* controller_;
    QThreadPool*      dspPool_;

    CombinedRxController* ctrl_{nullptr};

    // ── Active RX channels + gains (set by DeviceDetailWindow) ───────────────
    QList<ChannelDescriptor> activeChannels_;
    QVector<double>          gainsDb_;

    // ── Frequency controls ───────────────────────────────────────────────────
    FrequencyDial*  freqDial_{nullptr};

    // ── FFT plot ─────────────────────────────────────────────────────────────
    QCustomPlot*    fftPlot_{nullptr};
    QCPItemLine*    centerLine_{nullptr};
    QVector<QCPItemRect*> vfoBands_;    // one per DemodulatorPanel, index = slot
    bool            plotUserZoomed_{false};
    bool            fftDirty_{false};

    // ── VFO drag state (спектр + водопад) ────────────────────────────────────
    int             dragPanelIndex_{-1};      // индекс в panels_, -1 = нет драга
    double          dragGrabOffsetMHz_{0.0};  // mhz нажатия − VFO (без прыжка)
    bool            dragResize_{false};       // true = тянем край (ширина), false = сдвиг VFO

    // ── Demodulator panels ───────────────────────────────────────────────────
    QVBoxLayout*    panelsLayout_{nullptr};
    QVector<DemodulatorPanel*> panels_;
    QPushButton*    addDemodBtn_{nullptr};

    // ── Stream controls ──────────────────────────────────────────────────────
    QPushButton*    startBtn_{nullptr};
    QPushButton*    stopBtn_{nullptr};
    QLabel*         statusLabel_{nullptr};

    // ── Фазовая синхронизация каналов (виден при ≥2 RX) ─────────────────────
    QWidget*        phaseRow_{nullptr};
    QLabel*         phaseMetricLabel_{nullptr};   // live raw/residual/coherence
    QLabel*         phaseCalLabel_{nullptr};      // применённая калибровка
    QPushButton*    phaseCalBtn_{nullptr};
    QPushButton*    phaseResetBtn_{nullptr};
    QCheckBox*      phaseAutoCheck_{nullptr};    // автокалибровка при coh ≥ 0.9
    double          phaseCalDeg_{0.0};

    // ── Recording ────────────────────────────────────────────────────────────
    QCheckBox*        recordCheck_{nullptr};
    QPushButton*      settingsBtn_{nullptr};
    RecordingSettings recordingSettings_{};
    QString           sessionTimestamp_;     // set at startStream, reused for mid-session panels

    // ── Waterfall ────────────────────────────────────────────────────────────
    WaterfallView*    waterfallView_{nullptr};
    WaterfallHandler* waterfallHandler_{nullptr};
    QPushButton*      waterfallBtn_{nullptr};
    WaterfallSettings waterfallSettings_{};

    static constexpr int    kMaxDemods       = 4;
    static constexpr double kFreqMinMHz      =   30.0;
    static constexpr double kFreqMaxMHz      = 3800.0;
    static constexpr double kFreqDefaultMHz  =  102.0;
};
