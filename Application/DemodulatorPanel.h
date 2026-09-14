#pragma once

#include "../Core/DeviceSettings.h"

#include <QWidget>
#include <QString>
#include <QVector>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSlider;
class QLabel;
class QPushButton;
class QHBoxLayout;
class ModemHandler;
class FmAudioOutput;
class CombinedRxController;
class BandpassHandler;
class AudioFileHandler;

class DemodulatorPanel : public QWidget {
    Q_OBJECT

public:
    explicit DemodulatorPanel(int slotIndex, QWidget* parent = nullptr);
    ~DemodulatorPanel() override;

    void attachToController(CombinedRxController* ctrl);
    void detachFromController();

    void onStreamStarted();
    void onStreamStopped();
    void updateMetrics();

    void setCenterFreqMHz(double mhz);
    void setSampleRateHz(double sr);
    void tuneToMHz(double mhz);
    [[nodiscard]] double vfoFreqMHz() const;
    [[nodiscard]] int slotIndex() const { return slotIndex_; }
    [[nodiscard]] QString currentMode() const;
    [[nodiscard]] double currentBwMHz() const;
    // No-op if the current mode has no spin-based "Bandwidth" param.
    void setBandwidthHz(double hz);

    // Supplies everything needed to build recording filenames. Called by
    // RadioMonitorPage at stream start / settings change.
    //   dir            — output directory (empty disables recording)
    //   timestamp      — session prefix (YYYYMMDD_HHMMSS)
    //   combinedSource — rx0 / dualrx / triplerx / quadrorx
    //   centerFreqHz   — LO at time of session
    //   filteredAllowed/audioAllowed — master enables from RecordingSettings
    void setRecordingContext(const QString& dir,
                             const QString& timestamp,
                             const QString& combinedSource,
                             double         centerFreqHz,
                             bool           filteredAllowed,
                             bool           audioAllowed);

    // Persistence — mode/VFO/BW/volume/recording checkbox state.
    [[nodiscard]] DemodPanelSettings state() const;
    void applyState(const DemodPanelSettings& s);

signals:
    void removeRequested(int slotIndex);
    void vfoChanged(int slotIndex, double freqMHz, double bwMHz);

private:
    // One control per ParamDesc reported by the active modem handler.
    struct ParamControl {
        QString         name;            // param key (matches ModemHandler)
        double          scale{1.0};      // internal = UI value × scale (Hz)
        QLabel*         label{nullptr};
        QDoubleSpinBox* spin {nullptr};  // set for SpinParam
        QComboBox*      combo{nullptr};  // set for ComboParam
    };

    void onModeChanged(int index);
    void applyDemod();
    void teardownDemod();
    void buildUi();
    void emitVfoChanged();

    // Rebuilds the parameter-widget row from the selected modem's descriptors.
    void rebuildParamWidgets(const QString& mode);
    // Internal (Hz-domain) value of one dynamic parameter control.
    [[nodiscard]] double paramInternalValue(const ParamControl& pc) const;
    // Internal Hz of the "Bandwidth" param, or 0 if the modem has none.
    [[nodiscard]] double bandwidthHz() const;

    void updateFilteredRecording();
    void updateAudioRecording();
    void teardownFilteredRecording();
    void teardownAudioRecording();
    [[nodiscard]] bool recordingDirValid() const;

    int slotIndex_;
    double centerFreqMHz_{102.0};
    double sampleRateHz_{0.0};
    CombinedRxController* ctrl_{nullptr};

    ModemHandler* demodHandler_{nullptr};
    FmAudioOutput* audioOut_{nullptr};
    float volume_{0.8f};

    QComboBox*      modeCombo_{nullptr};
    QDoubleSpinBox* vfoSpin_{nullptr};
    QSlider*        volumeSlider_{nullptr};
    QLabel*         volumeLabel_{nullptr};
    QLabel*         statusLabel_{nullptr};
    QLabel*         levelLabel_{nullptr};
    QPushButton*    removeButton_{nullptr};

    // ── Dynamic per-modem parameter widgets ─────────────────────────────────
    // The row is rebuilt whenever the mode changes; no modem-specific UI code.
    QWidget*              paramHost_  {nullptr};
    QHBoxLayout*          paramLayout_{nullptr};
    QVector<ParamControl> params_;

    // ── Recording ───────────────────────────────────────────────────────────
    QCheckBox*        filteredCheck_{nullptr};
    QCheckBox*        audioCheck_   {nullptr};
    BandpassHandler*  filteredHandler_{nullptr};   // owned, attached via addExtraHandler
    AudioFileHandler* audioHandler_   {nullptr};   // owned
    QString           recordingDir_;
    QString           recordingTimestamp_;
    QString           combinedSource_;
    double            recordingCenterHz_{0.0};
    bool              filteredAllowed_{false};
    bool              audioAllowed_   {false};
};
