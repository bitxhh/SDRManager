#include "DemodulatorPanel.h"
#include "CombinedRxController.h"
#include "../Audio/FmAudioOutput.h"
#include "../Core/FileNaming.h"
#include "../DSP/AudioFileHandler.h"
#include "../DSP/BandpassHandler.h"
#include "../DSP/ModemHandler.h"
#include "../DSP/ModemRegistry.h"
#include "../DSP/ModemTypes.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>
#include <variant>

namespace {
// Number of decimal places needed to represent `step` exactly (capped at 4).
int decimalsForStep(double step) {
    int d = 0;
    double s = std::abs(step);
    while (d < 4 && std::abs(s - std::round(s)) > 1e-9) { s *= 10.0; ++d; }
    return d;
}
} // namespace

DemodulatorPanel::DemodulatorPanel(int slotIndex, QWidget* parent)
    : QWidget(parent)
    , slotIndex_(slotIndex)
{
    buildUi();
}

DemodulatorPanel::~DemodulatorPanel() {
    detachFromController();
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::buildUi() {
    setObjectName("DemodulatorPanel");
    setStyleSheet(
        "QWidget#DemodulatorPanel { "
        "background-color: #2a2a2a; border: 1px solid #444; border-radius: 4px; "
        "}");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 6, 4);
    outer->setSpacing(2);

    // в”Ђв”Ђ Row 1: slot label, mode, VFO, BW/De-emph, volume, remove в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    auto* row1  = new QWidget(this);
    auto* hlay1 = new QHBoxLayout(row1);
    hlay1->setContentsMargins(0, 0, 0, 0);

    auto* slotLabel = new QLabel(QString("Demod %1").arg(slotIndex_ + 1), row1);
    slotLabel->setStyleSheet("font-weight: 600; color: #cccccc;");
    slotLabel->setFixedWidth(60);

    auto* modeLabel = new QLabel("Mode:", row1);
    modeCombo_ = new QComboBox(row1);
    modeCombo_->addItem("Off");                       // index 0 = disabled
    modeCombo_->addItems(ModemRegistry::instance().names());
    modeCombo_->setFixedWidth(64);

    auto* vfoLabel = new QLabel("VFO (MHz):", row1);
    vfoSpin_ = new QDoubleSpinBox(row1);
    vfoSpin_->setRange(30.0, 3800.0);
    vfoSpin_->setDecimals(3);
    vfoSpin_->setSingleStep(0.025);
    vfoSpin_->setValue(102.0);
    vfoSpin_->setFixedWidth(100);
    vfoSpin_->setEnabled(false);

    // Host for the dynamic per-modem parameter widgets (rebuilt on mode change).
    paramHost_   = new QWidget(row1);
    paramLayout_ = new QHBoxLayout(paramHost_);
    paramLayout_->setContentsMargins(0, 0, 0, 0);
    paramLayout_->setSpacing(6);

    auto* volLabel = new QLabel("Vol:", row1);
    volumeSlider_  = new QSlider(Qt::Horizontal, row1);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(80);
    volumeSlider_->setFixedWidth(80);
    volumeLabel_ = new QLabel("80%", row1);
    volumeLabel_->setFixedWidth(36);

    filteredCheck_ = new QCheckBox(tr("Rec filt"), row1);
    filteredCheck_->setToolTip(
        tr("Record filtered I/Q around this demodulator's VFO.\n"
           "Enable 'Filtered I/Q' in recording settings to activate."));
    filteredCheck_->setEnabled(false);

    audioCheck_ = new QCheckBox(tr("Rec audio"), row1);
    audioCheck_->setToolTip(
        tr("Record demodulated audio (mono float32 WAV).\n"
           "Enable 'Audio' in recording settings to activate."));
    audioCheck_->setEnabled(false);

    removeButton_ = new QPushButton("\u2715", row1);
    removeButton_->setFixedWidth(26);
    removeButton_->setToolTip("Remove demodulator");

    hlay1->addWidget(slotLabel);
    hlay1->addWidget(modeLabel);
    hlay1->addWidget(modeCombo_);
    hlay1->addSpacing(8);
    hlay1->addWidget(vfoLabel);
    hlay1->addWidget(vfoSpin_);
    hlay1->addSpacing(8);
    hlay1->addWidget(paramHost_);
    hlay1->addSpacing(8);
    hlay1->addWidget(volLabel);
    hlay1->addWidget(volumeSlider_);
    hlay1->addWidget(volumeLabel_);
    hlay1->addSpacing(8);
    hlay1->addWidget(filteredCheck_);
    hlay1->addWidget(audioCheck_);
    hlay1->addStretch();
    hlay1->addWidget(removeButton_);

    outer->addWidget(row1);

    // в”Ђв”Ђ Row 2: status + IF level в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    auto* row2  = new QWidget(this);
    auto* hlay2 = new QHBoxLayout(row2);
    hlay2->setContentsMargins(0, 0, 0, 0);

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: gray; font-size: 11px;");

    levelLabel_ = new QLabel(
        "\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF", this);
    levelLabel_->setStyleSheet("color: gray; font-size: 10px;");

    hlay2->addWidget(statusLabel_, 1);
    hlay2->addWidget(levelLabel_);
    outer->addWidget(row2);

    // в”Ђв”Ђ Wiring в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DemodulatorPanel::onModeChanged);

    connect(vfoSpin_, &QDoubleSpinBox::valueChanged, this, [this](double mhz) {
        if (demodHandler_) {
            const double offsetHz = (mhz - centerFreqMHz_) * 1e6;
            demodHandler_->setOffset(offsetHz);
        }
        emitVfoChanged();
    });

    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int v) {
        volumeLabel_->setText(QString("%1%").arg(v));
        volume_ = static_cast<float>(v) / 100.0f;
        if (audioOut_) audioOut_->setVolume(volume_);
    });

    connect(removeButton_, &QPushButton::clicked, this, [this]() {
        emit removeRequested(slotIndex_);
    });

    connect(filteredCheck_, &QCheckBox::toggled,
            this, [this](bool) { updateFilteredRecording(); });
    connect(audioCheck_, &QCheckBox::toggled,
            this, [this](bool) { updateAudioRecording(); });
}

// ---------------------------------------------------------------------------
// Tears down the previous parameter row and rebuilds it from the selected
// modem's descriptors. Each widget pushes changes straight into the live
// handler (if any), so no modem-specific UI code is ever needed here.
void DemodulatorPanel::rebuildParamWidgets(const QString& mode) {
    // Drop the old controls.
    params_.clear();
    if (paramLayout_) {
        QLayoutItem* item;
        while ((item = paramLayout_->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    }
    if (mode.isEmpty() || !paramLayout_) return;

    // Query the descriptors from a throwaway handler (offset irrelevant here).
    std::unique_ptr<ModemHandler> proto(
        ModemRegistry::instance().create(mode, 0.0, nullptr));
    if (!proto) return;

    for (const modem::ParamDesc& desc : proto->paramDescriptors()) {
        ParamControl pc;

        if (const auto* sp = std::get_if<modem::SpinParam>(&desc)) {
            pc.name  = sp->name;
            pc.scale = sp->scale;

            pc.label = new QLabel(sp->name + ':', paramHost_);
            pc.label->setStyleSheet("color: #cccccc;");

            pc.spin = new QDoubleSpinBox(paramHost_);
            pc.spin->setRange(sp->min, sp->max);
            pc.spin->setSingleStep(sp->step);
            pc.spin->setDecimals(decimalsForStep(sp->step));
            pc.spin->setSuffix(sp->suffix);
            pc.spin->setValue(sp->defaultVal);

            const QString name  = sp->name;
            const double  scale = sp->scale;
            connect(pc.spin, &QDoubleSpinBox::valueChanged, this,
                    [this, name, scale](double v) {
                        if (demodHandler_) demodHandler_->setParam(name, v * scale);
                        emitVfoChanged();   // Bandwidth may have changed.
                    });

            paramLayout_->addWidget(pc.label);
            paramLayout_->addWidget(pc.spin);
        }
        else if (const auto* cp = std::get_if<modem::ComboParam>(&desc)) {
            pc.name  = cp->name;
            pc.scale = 1.0;

            pc.label = new QLabel(cp->name + ':', paramHost_);
            pc.label->setStyleSheet("color: #cccccc;");

            pc.combo = new QComboBox(paramHost_);
            for (const auto& opt : cp->options)
                pc.combo->addItem(opt.label, opt.value);
            pc.combo->setCurrentIndex(
                std::clamp(cp->defaultIndex, 0, int(cp->options.size()) - 1));

            const QString name = cp->name;
            connect(pc.combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, name](int) {
                        if (!demodHandler_) return;
                        // currentData() carries the option's internal value.
                        for (const ParamControl& p : params_)
                            if (p.name == name && p.combo)
                                demodHandler_->setParam(
                                    name, p.combo->currentData().toDouble());
                    });

            paramLayout_->addWidget(pc.label);
            paramLayout_->addWidget(pc.combo);
        }
        else {
            continue;
        }

        params_.push_back(pc);
    }
}

// ---------------------------------------------------------------------------
double DemodulatorPanel::paramInternalValue(const ParamControl& pc) const {
    if (pc.spin)  return pc.spin->value() * pc.scale;
    if (pc.combo) return pc.combo->currentData().toDouble();
    return 0.0;
}

// ---------------------------------------------------------------------------
double DemodulatorPanel::bandwidthHz() const {
    for (const ParamControl& pc : params_)
        if (pc.name == QLatin1String("Bandwidth"))
            return paramInternalValue(pc);
    return 0.0;
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::attachToController(CombinedRxController* ctrl) {
    if (ctrl_ == ctrl) return;
    detachFromController();
    ctrl_ = ctrl;
    // If mode is already active, rebuild the handler against the new controller.
    if (ctrl_ && modeCombo_ && modeCombo_->currentIndex() != 0)
        applyDemod();
}

void DemodulatorPanel::detachFromController() {
    teardownFilteredRecording();
    teardownDemod();
    ctrl_ = nullptr;
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::onModeChanged(int index) {
    const bool active = (index != 0);

    // Build the parameter-widget row for the newly selected modem (empty when Off).
    rebuildParamWidgets(currentMode());
    vfoSpin_->setEnabled(active);

    teardownDemod();
    if (statusLabel_) statusLabel_->setText("");

    if (active && ctrl_ && ctrl_->isStreaming())
        applyDemod();

    emitVfoChanged();
}

// ---------------------------------------------------------------------------
bool DemodulatorPanel::recordingDirValid() const {
    return !recordingDir_.isEmpty() && !recordingTimestamp_.isEmpty();
}

void DemodulatorPanel::setRecordingContext(const QString& dir,
                                           const QString& timestamp,
                                           const QString& combinedSource,
                                           double         centerFreqHz,
                                           bool           filteredAllowed,
                                           bool           audioAllowed)
{
    recordingDir_        = dir;
    recordingTimestamp_  = timestamp;
    combinedSource_      = combinedSource;
    recordingCenterHz_   = centerFreqHz;
    filteredAllowed_     = filteredAllowed;
    audioAllowed_        = audioAllowed;

    if (filteredCheck_) {
        filteredCheck_->setEnabled(filteredAllowed);
        if (!filteredAllowed && filteredCheck_->isChecked())
            filteredCheck_->setChecked(false);   // triggers teardown via toggled signal
    }
    if (audioCheck_) {
        audioCheck_->setEnabled(audioAllowed);
        if (!audioAllowed && audioCheck_->isChecked())
            audioCheck_->setChecked(false);
    }

    // Re-evaluate вЂ” paths may have become valid / invalid.
    updateFilteredRecording();
    updateAudioRecording();
}

void DemodulatorPanel::updateFilteredRecording() {
    teardownFilteredRecording();

    if (!ctrl_ || !ctrl_->isStreaming()) return;
    if (!filteredCheck_ || !filteredCheck_->isChecked()) return;
    if (!filteredAllowed_ || !recordingDirValid())      return;

    const double bwMHz = currentBwMHz();
    if (bwMHz <= 0.0) return;   // no active mode

    const double bwHz    = bwMHz * 1e6;
    const double bwKHz   = bwMHz * 1e3;
    const double vfoHz   = (vfoFreqMHz() - centerFreqMHz_) * 1e6;
    constexpr double kOutputSR = 250'000.0;   // BandpassExporter default

    const QString suffix = QStringLiteral("bp%1kHz").arg(bwKHz, 0, 'f', 0);
    const QString path = FileNaming::composeWithSuffix(
        recordingDir_, recordingTimestamp_, combinedSource_, suffix,
        recordingCenterHz_, kOutputSR, ".cf32");

    filteredHandler_ = new BandpassHandler(path, vfoHz, bwHz, kOutputSR);
    ctrl_->addExtraHandler(filteredHandler_);
}

void DemodulatorPanel::teardownFilteredRecording() {
    if (!filteredHandler_) return;
    if (ctrl_) ctrl_->removeExtraHandler(filteredHandler_);
    delete filteredHandler_;
    filteredHandler_ = nullptr;
}

void DemodulatorPanel::updateAudioRecording() {
    teardownAudioRecording();

    if (!ctrl_ || !ctrl_->isStreaming() || !demodHandler_) return;
    if (!audioCheck_ || !audioCheck_->isChecked()) return;
    if (!audioAllowed_ || !recordingDirValid())   return;

    const QString mode = currentMode().toLower();   // "fm" / "am"
    if (mode.isEmpty()) return;

    const QString suffix = QStringLiteral("%1%2").arg(mode).arg(slotIndex_);
    const QString dir    = recordingDir_;
    const QString ts     = recordingTimestamp_;
    const QString src    = combinedSource_;
    const double  cf     = recordingCenterHz_;

    auto builder = [dir, ts, src, suffix, cf](double sr) {
        return FileNaming::composeWithSuffix(dir, ts, src, suffix, cf, sr, ".wav");
    };

    audioHandler_ = new AudioFileHandler(builder, this);
    connect(demodHandler_, &ModemHandler::audioReady,
            audioHandler_, &AudioFileHandler::push, Qt::QueuedConnection);
}

void DemodulatorPanel::teardownAudioRecording() {
    if (!audioHandler_) return;
    if (demodHandler_)
        disconnect(demodHandler_, &ModemHandler::audioReady,
                   audioHandler_, &AudioFileHandler::push);
    audioHandler_->close();
    delete audioHandler_;
    audioHandler_ = nullptr;
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::applyDemod() {
    if (!ctrl_) return;
    const QString modeStr = currentMode();
    if (modeStr.isEmpty()) return;

    const double offsetHz = (vfoSpin_->value() - centerFreqMHz_) * 1e6;

    demodHandler_ = ModemRegistry::instance().create(modeStr, offsetHz, this);
    if (!demodHandler_) return;

    // Push current param values into the handler before it's added to pipeline.
    for (const ParamControl& pc : params_)
        demodHandler_->setParam(pc.name, paramInternalValue(pc));

    audioOut_ = new FmAudioOutput(this);
    audioOut_->setVolume(volume_);
    connect(audioOut_, &FmAudioOutput::statusChanged,
            this, [this](const QString& msg, bool isError) {
                if (!statusLabel_) return;
                statusLabel_->setStyleSheet(
                    isError ? "color: #ff4444; font-size: 11px;"
                            : "color: #00cc44; font-size: 11px;");
                statusLabel_->setText(msg);
            });

    connect(demodHandler_, &ModemHandler::audioReady,
            audioOut_,     &FmAudioOutput::push, Qt::QueuedConnection);

    ctrl_->addExtraHandler(demodHandler_);

    if (statusLabel_) {
        statusLabel_->setStyleSheet("color: gray; font-size: 11px;");
        statusLabel_->setText(modeStr + ": waiting for first audio block\u2026");
    }

    // Hook up audio recording now that demodHandler_ emits audioReady.
    updateAudioRecording();
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::teardownDemod() {
    // Close audio WAV before the demod handler goes away (audioReady would
    // otherwise deliver to a deleted object via queued connection).
    teardownAudioRecording();

    if (ctrl_ && demodHandler_)
        ctrl_->removeExtraHandler(demodHandler_);

    delete demodHandler_;
    demodHandler_ = nullptr;

    if (audioOut_) {
        audioOut_->teardown();
        delete audioOut_;
        audioOut_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::setCenterFreqMHz(double mhz) {
    centerFreqMHz_ = mhz;
    if (vfoSpin_) {
        const double half = (sampleRateHz_ > 0 ? sampleRateHz_ / 2.0 : 2e6) / 1e6;
        const double curMHz = vfoSpin_->value();
        QSignalBlocker b(vfoSpin_);
        vfoSpin_->setRange(mhz - half, mhz + half);
        // Keep VFO near the new center when it would otherwise fall outside band.
        const double clamped = std::clamp(curMHz, mhz - half, mhz + half);
        vfoSpin_->setValue(clamped);
    }
    if (demodHandler_) {
        const double offsetHz = (vfoSpin_->value() - centerFreqMHz_) * 1e6;
        demodHandler_->setOffset(offsetHz);
    }
    emitVfoChanged();
}

void DemodulatorPanel::setSampleRateHz(double sr) {
    sampleRateHz_ = sr;
    if (vfoSpin_) {
        const double half = (sr > 0 ? sr / 2.0 : 2e6) / 1e6;
        const double cur  = vfoSpin_->value();
        QSignalBlocker b(vfoSpin_);
        vfoSpin_->setRange(centerFreqMHz_ - half, centerFreqMHz_ + half);
        vfoSpin_->setValue(std::clamp(cur, centerFreqMHz_ - half, centerFreqMHz_ + half));
    }
    emitVfoChanged();
}

void DemodulatorPanel::tuneToMHz(double mhz) {
    if (!vfoSpin_) return;
    const double half = (sampleRateHz_ > 0 ? sampleRateHz_ / 2.0 : 2e6) / 1e6;
    const double clamped = std::clamp(mhz, centerFreqMHz_ - half, centerFreqMHz_ + half);
    vfoSpin_->setValue(clamped);   // triggers valueChanged в†’ offset update + vfoChanged signal
}

double DemodulatorPanel::vfoFreqMHz() const {
    return vfoSpin_ ? vfoSpin_->value() : centerFreqMHz_;
}

QString DemodulatorPanel::currentMode() const {
    return (modeCombo_ && modeCombo_->currentIndex() != 0)
               ? modeCombo_->currentText()
               : QString{};
}

double DemodulatorPanel::currentBwMHz() const {
    return bandwidthHz() / 1e6;
}

void DemodulatorPanel::setBandwidthHz(double hz) {
    for (ParamControl& pc : params_) {
        if (pc.name != QLatin1String("Bandwidth") || !pc.spin || pc.scale == 0.0) continue;
        // The spin clamps to the modem's range; valueChanged pushes the param
        // to the handler and emits vfoChanged (band redraw).
        pc.spin->setValue(hz / pc.scale);
        return;
    }
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::emitVfoChanged() {
    emit vfoChanged(slotIndex_, vfoFreqMHz(), currentBwMHz());
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::onStreamStarted() {
    // Re-attach the demodulator to the (newly created) combined pipeline.
    if (ctrl_ && modeCombo_ && modeCombo_->currentIndex() != 0)
        applyDemod();

    // Filtered recording is independent of the demod вЂ” attach if the user
    // had its checkbox on (audio recording is handled inside applyDemod()).
    updateFilteredRecording();
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::onStreamStopped() {
    if (statusLabel_) statusLabel_->setText("");
    if (levelLabel_)
        levelLabel_->setText("\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF\u25AF");

    // Close any recording handlers so their files are finalized. Ownership
    // of filteredHandler_ / audioHandler_ lives with the panel вЂ” the controller
    // only holds raw pointers in extraHandlers_ and does not delete them.
    teardownAudioRecording();
    teardownFilteredRecording();

    // Handler will be cleaned up by CombinedRxController::performCleanup via the
    // extraHandlers_ list. Drop our pointer so we don't double-delete.
    demodHandler_ = nullptr;
    if (audioOut_) {
        audioOut_->teardown();
        delete audioOut_;
        audioOut_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
DemodPanelSettings DemodulatorPanel::state() const {
    DemodPanelSettings s;
    s.mode = (modeCombo_ && modeCombo_->currentIndex() != 0)
                 ? modeCombo_->currentText()
                 : QStringLiteral("Off");
    if (vfoSpin_)       s.vfoMHz         = vfoSpin_->value();
    if (volumeSlider_)  s.volumePct      = volumeSlider_->value();
    if (filteredCheck_) s.recordFiltered = filteredCheck_->isChecked();
    if (audioCheck_)    s.recordAudio    = audioCheck_->isChecked();

    // Dynamic per-modem params: UI value for spins, internal value for combos.
    for (const ParamControl& pc : params_) {
        if (pc.spin)       s.params.insert(pc.name, pc.spin->value());
        else if (pc.combo) s.params.insert(pc.name, pc.combo->currentData().toDouble());
    }
    return s;
}

void DemodulatorPanel::applyState(const DemodPanelSettings& s) {
    if (vfoSpin_) {
        QSignalBlocker b(vfoSpin_);
        // Range may be narrower than the saved value until setSampleRateHz()
        // is called with the real SR; clamp silently here, the page widens
        // the range later.
        const double lo = vfoSpin_->minimum();
        const double hi = vfoSpin_->maximum();
        vfoSpin_->setValue(std::clamp(s.vfoMHz, lo, hi));
    }
    if (volumeSlider_) {
        QSignalBlocker b(volumeSlider_);
        volumeSlider_->setValue(std::clamp(s.volumePct,
            volumeSlider_->minimum(), volumeSlider_->maximum()));
    }
    volume_ = static_cast<float>(s.volumePct) / 100.0f;
    if (volumeLabel_) volumeLabel_->setText(QString("%1%").arg(s.volumePct));

    if (filteredCheck_) { QSignalBlocker b(filteredCheck_); filteredCheck_->setChecked(s.recordFiltered); }
    if (audioCheck_)    { QSignalBlocker b(audioCheck_);    audioCheck_->setChecked(s.recordAudio); }

    // Select the mode вЂ” onModeChanged rebuilds the param row with defaults,
    // then we overwrite those defaults with any saved values below.
    if (modeCombo_) {
        const int idx = (s.mode.isEmpty() || s.mode == QStringLiteral("Off"))
                            ? 0
                            : std::max(0, modeCombo_->findText(s.mode));
        modeCombo_->setCurrentIndex(idx);
    }

    // Restore saved param values onto the freshly-built widgets.
    for (const ParamControl& pc : params_) {
        const auto it = s.params.find(pc.name);
        if (it == s.params.end()) continue;
        if (pc.spin) {
            QSignalBlocker b(pc.spin);
            pc.spin->setValue(it.value());
        } else if (pc.combo) {
            const int di = pc.combo->findData(it.value());
            if (di >= 0) { QSignalBlocker b(pc.combo); pc.combo->setCurrentIndex(di); }
        }
    }
}

// ---------------------------------------------------------------------------
void DemodulatorPanel::updateMetrics() {
    if (!levelLabel_ || !demodHandler_) return;
    const double ifRms = demodHandler_->ifRms();
    levelLabel_->setText(QString("IF %1").arg(ifRms, 0, 'f', 3));
}
