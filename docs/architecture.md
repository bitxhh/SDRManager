# Architecture

## Component diagram

```
main.cpp
  ├── CompositeDeviceManager     — LimeSDR + SoapySDR + FileDevice (I/Q playback)
  └── Application                — QApplication + DeviceSelectionWindow + SessionManager
        └── DeviceDetailWindow   — main UI window per device (4-page navigation)
              ├── DeviceController     — thin command layer (UI → IDevice), no widgets
              ├── QThreadPool (dspPool_) — shared across all pipelines
              │
              ├── RadioMonitorPage     — Радиомониторинг page (owns CombinedRxController)
              │     └── CombinedRxController
              │           ├── [per RX channel] RxWorker (QThread) + PrePipeline
              │           │     └── IqCombiner (IPipelineHandler in each PrePipeline)
              │           └── Combined Pipeline  — receives merged I/Q
              │                 ├── FftHandler              → spectrum display
              │                 ├── WaterfallHandler        → waterfall display
              │                 ├── RawFileHandler          → combined .cf32 I/Q dump
              │                 └── [via addExtraHandler, per DemodulatorPanel]
              │                       ├── ModemHandler (FM/NFM/AM/SAM/USB/LSB/CW) → audio demodulator
              │                       ├── BandpassHandler      → filtered .cf32
              │                       └── AudioFileHandler     → .wav recording
              │
              └── TxController
                    └── TxWorker (QThread)   — blocking I/Q transmit loop
                          └── ITxSource (ToneSource / ...) — I/Q generator
```

> **Single-channel mode**: only one `RxWorker` + `PrePipeline`; `IqCombiner` with
> `channelCount=1` is a no-op pass-through — the same code path runs either way.

## Directory layout

```
Core/               Interfaces and infrastructure
  IDevice.h             SDR-agnostic device interface (channel-aware: RX0/RX1/TX0/TX1)
  IDeviceManager.h      Device discovery interface
  IPipelineHandler.h    Signal processing handler interface + BlockMeta
  Pipeline.h/.cpp       float32 I/Q block router (shared_mutex + optional parallel dispatch)
  ChannelDescriptor.h   {Direction RX|TX, int channelIndex}
  ISyncController.h     3-level sync interface: clock / timestamp / trigger (stub)
  ITxSource.h           TX I/Q generator interface
  ILogger.h             Logger interface
  Logger.h/.cpp         Thread-safe singleton logger
  LoggerConfig.h/.cpp   Per-category log enable flags (persisted to JSON)
  LimeException.h       Exception hierarchy for LimeSuite errors
  DeviceSettings.h/.cpp Per-device JSON config (SR, gains, freq, demod panel states)
  RecordingSettings.h   Recording options (dir, format, enabled tracks)
  WaterfallSettings.h   Waterfall display options
  FileNaming.h/.cpp     Filename builder: {date}_{time}_{source}[_{suffix}]_{freq}_{sr}.{ext}

Hardware/           Device backends
  LimeDevice.h/.cpp             IDevice for LimeSDR (LimeSuite C API, both RX channels + TX)
  LimeDeviceManager.h/.cpp      IDeviceManager, USB scanning for LimeSDR
  LimeSyncController.h/.cpp     ISyncController stub
  SoapyApi.h/.cpp               Runtime loader for SoapySDR (QLibrary, optional)
  SoapyDevice.h/.cpp            IDevice for any SoapySDR device
  SoapyDeviceManager.h/.cpp     IDeviceManager, SoapySDR enumeration
  FileDevice.h/.cpp             IDevice that plays back .cf32/.cs16 recordings (looped, paced at SR)
  CompositeDeviceManager.h/.cpp Merges the managers above into one device list
  DeviceController.h/.cpp       Exception-safe UI→device command wrapper
  RxWorker.h/.cpp               QThread I/Q recv loop, pipeline dispatch (channel-aware)
  TxWorker.h/.cpp               QThread I/Q transmit loop

DSP/                Signal processing
  FftProcessor.h/.cpp        Stateless FFT (FFTW3 float32, AVX2+FMA, thread-local plan cache)
  FftPlannerLock.h           Global mutex around FFTW planner calls
  FftHandler.h/.cpp          IPipelineHandler: throttled FFT + EMA smoothing
  WaterfallHandler.h/.cpp    IPipelineHandler: FFT lines for the waterfall (emits lineReady)
  IModem.h                   Modem contract: name, param descriptors, makeModulator()
  IModulator.h               TX engine interface (audio → I/Q); no impls yet
  ChannelModem.h/.cpp        Common engine base: DC blocker, NCO, FIR, decimate
  ModemHandler.h/.cpp        Common handler base (IModem): IF RMS metrics, param dispatch
  ModemRegistry.h/.cpp       Factory registry: mode name → ModemHandler*
  ModemTypes.h               modem::ParamDesc (SpinParam/ComboParam) for UI auto-build
  FmModem.h/.cpp             WBFM demodulator engine (+ de-emphasis)
  FmModemHandler.h/.cpp      IPipelineHandler + IModem wrapper for FmModem
  NfmModem.h/.cpp            Narrowband FM demodulator engine
  NfmModemHandler.h/.cpp     Handler wrapper for NfmModem
  AmModem.h/.cpp             AM envelope demodulator engine
  AmModemHandler.h/.cpp      Handler wrapper for AmModem
  SamModem.h/.cpp            Synchronous AM demodulator engine (PLL carrier recovery)
  SamModemHandler.h/.cpp     Handler wrapper for SamModem
  SsbModem.h/.cpp            SSB demodulator engine (USB / LSB)
  SsbModemHandler.h/.cpp     Handler wrapper for SsbModem
  CwModem.h/.cpp             CW demodulator engine (configurable pitch)
  CwModemHandler.h/.cpp      Handler wrapper for CwModem
  DspUtils.h/.cpp            Shared DSP primitives
  IqCombiner.h/.cpp          N-channel gain-normalised I/Q combiner (→ combined Pipeline)
  BandpassExporter.h/.cpp    NCO + FIR + decimate → float32 writer
  BandpassHandler.h/.cpp     IPipelineHandler wrapper for BandpassExporter
  RawFileHandler.h/.cpp      IPipelineHandler: float32 I/Q dump (.cf32)
  AudioFileHandler.h/.cpp    Appends mono float32 audio to a WAV file
  ClassifierHandler.h/.cpp   Forwards I/Q blocks to AI classifier (optional)
  ToneGenerator.h/.cpp       ITxSource: sinusoid I/Q generator

Audio/              Audio output
  FmAudioOutput.h/.cpp       Linear resampler + AGC + QAudioSink (WASAPI)

Application/        UI (Qt widgets only — no DSP, no hardware calls)
  Application.h/.cpp             DeviceSelectionWindow + DeviceDetailWindow
  RadioMonitorPage.h/.cpp        Unified RX page: FFT + waterfall, DemodulatorPanel list
  WaterfallView.h/.cpp           Waterfall widget
  CombinedRxController.h/.cpp    Multi-channel coherent RX (PrePipelines → IqCombiner)
  RxController.h/.cpp            Single-channel RX (Pipeline + RxWorker + handlers)
  DemodulatorPanel.h/.cpp        Per-demodulator widget (mode, VFO, params, recording)
  RecordingSettingsDialog.h/.cpp Dialog for recording path + format + track selection
  LoggerOptionsDialog.h/.cpp     Dialog for per-category logging (LoggerConfig)
  TxController.h/.cpp            Owns TxWorker + ITxSource
  ClassifierController.h/.cpp    Python subprocess + TCP socket → ClassifierHandler
  SessionManager.h/.cpp          Tracks which device IDs have open windows
  ChannelPanel.h/.cpp            Legacy single-channel panel (kept for compatibility)
```

## Threading model

| Thread | Components | Responsibilities |
|--------|-----------|-----------------|
| **Main (Qt event loop)** | All widgets, DeviceController, FmAudioOutput, TxController | UI updates, audio sink writes, device commands, prepareStream (LimeSuite quirk) |
| **RxWorker (QThread)** — one per RX channel | RxWorker, PrePipeline dispatch | Blocking `readBlock()`, int16→float conversion, PrePipeline dispatch |
| **QThreadPool (dspPool_)** | IPipelineHandler tasks in combined Pipeline | Parallel handler execution: FFT, ModemHandlers, RawFileHandler run concurrently per block |
| **TxWorker (QThread)** | TxWorker, ITxSource | `generateBlock()` + `writeBlock()` loop |

Cross-thread signals: `Qt::QueuedConnection`. No shared mutable state between handlers.

**LimeSuite prepareStream quirk:** `LMS_SetupStream` must be called from the UI thread before
workers start — it stops all active streams. `DeviceDetailWindow` calls `device->prepareStream(ch)`
for all RX channels before launching workers. Workers call only `LMS_StartStream` / `LMS_StopStream`.

## Key interfaces

**IDevice** — SDR-agnostic device abstraction (channel-aware):
- `init()` / `calibrate()` / `setSampleRate()` / `setFrequency(ch)` / `setGain(ch)`
- `prepareStream(ch)` — UI thread only (LMS_SetupStream)
- `startStream(ch)` / `readBlock(ch, ...)` / `stopStream(ch)` — worker thread
- `writeBlock(ch, ...)` — TxWorker thread
- `lastReadTimestamp(ch)` — hardware sample counter for sync
- State machine: `Disconnected → Connected → Ready → Streaming → Error`

**IPipelineHandler** — signal processing stage (float32):
```cpp
virtual void processBlock(const float* iq, int count, double sampleRateHz) = 0;
virtual void processBlock(const float* iq, int count, double sampleRateHz,
                          const BlockMeta& meta);   // default: delegates above
virtual void onStreamStarted(double sampleRateHz) {}
virtual void onStreamStopped() {}
virtual void onRetune(double newFreqHz) {}
```
`iq` is interleaved float32 `[I0, Q0, I1, Q1, ...]` normalised to `[-1, 1]`.  
When `Pipeline` has a `QThreadPool*`, each handler is a separate pool task (concurrent).

**Pipeline** — reader-writer lock router with optional parallel dispatch:
- `addHandler()` / `removeHandler()` — exclusive lock
- `dispatchBlock(const float*, ...)` — shared lock; parallel if `pool != nullptr && handlers > 1`
- Parallel path: `QtConcurrent::run(pool, lambda)` per handler + `waitForFinished()` barrier
- Sequential fallback: `pool == nullptr` or single handler (TX pipeline, tests)

**BlockMeta** — per-block metadata:
```cpp
struct BlockMeta {
    ChannelDescriptor channel{};    // which RX channel produced this block
    uint64_t          timestamp{0}; // hardware sample counter (0 = unavailable)
};
```

**IqCombiner** — N-channel coherent combiner (IPipelineHandler in each PrePipeline):
- Registered in each per-channel `PrePipeline`; called from different `RxWorker` threads
- Internal mutex serialises slots; thread filling the last slot performs combine+dispatch
- Gain normalisation: `scale = 1 / 10^(gainDb / 20)` before averaging
- Timestamp-matching: waits until all N channels deliver a block; unmatched blocks are dropped after a timeout (ring-buffer, 2–4 slots per channel)
- Both RX channels share one RXPLL on LimeSDR → coherent I/Q → pre-detection averaging valid

**CombinedRxController** — multi-channel RX lifecycle:
- Creates one `RxWorker + PrePipeline` per channel, with `IqCombiner` as the last PrePipeline handler
- Owns combined `Pipeline`, `FftHandler`, optional `RawFileHandler`
- `addExtraHandler()` / `removeExtraHandler()` — used by `DemodulatorPanel` at runtime
- Single-channel: same code path, `IqCombiner(1, ...)` is a pass-through

**RxController** — single-channel RX (legacy / used by ChannelPanel):
- Same API surface as `CombinedRxController` but without IqCombiner
- Kept for `ClassifierController` integration and ChannelPanel compatibility

**DemodulatorPanel** — per-demodulator UI slot (max 4):
- Owns its own `ModemHandler`, `FmAudioOutput`, `BandpassHandler`, `AudioFileHandler`
- Attaches/detaches from `CombinedRxController` via `addExtraHandler`
- Emits `vfoChanged` → `RadioMonitorPage` updates VFO band overlay on FFT plot

**Device backends:**
- `CompositeDeviceManager` merges `LimeDeviceManager`, `SoapyDeviceManager` and file playback into one list
- `SoapyApi` loads `SoapySDR.dll` at runtime (`QLibrary`); if it is missing, SoapySDR devices are simply not listed
- `FileDevice` plays `.cf32` (interleaved float32) or `.cs16` (interleaved int16) files; center frequency
  and sample rate are parsed from the SDRManager filename, blocks are paced at the file sample rate and
  the file loops at EOF; the timestamp is a monotonic sample counter

**DeviceSettings / persistence:**
- JSON per device serial: SR, channel count, gains, center freq, TX settings, demod panel states
- INI per device serial: LimeSuite chip register dump (`LMS_SaveConfig` / `LMS_LoadConfig`)
- Storage: `<AppDataLocation>/SDRManager/devices/<serial>.{json,ini}`

## Data flow

### I/Q → Spectrum (multi-channel)
```
RxWorker CH0 → int16→float → PrePipeline CH0 → IqCombiner ──┐
RxWorker CH1 → int16→float → PrePipeline CH1 → IqCombiner ──┴→ Combined Pipeline
                                                                        ↓
                                                               [pool task] FftHandler
                                                                  ├─ Throttle 30 fps
                                                                  ├─ Hann window
                                                                  ├─ FFTW forward (float32, AVX2+FMA)
                                                                  ├─ FFT-shift (DC center)
                                                                  ├─ Power → dBFS
                                                                  └─ EMA smooth (α=0.1)
                                                                         ↓ emit fftReady()
                                                               RadioMonitorPage::onFftReady()
                                                               QCustomPlot::replot() (plotTimer 50ms)
```

### I/Q → Audio (all modes) — parallel with FFT, per DemodulatorPanel
```
Combined Pipeline → [pool task] ModemHandler → ChannelModem subclass (FM/NFM/AM/SAM/SSB/CW)
                                                               ↓
                                                   QVector<float> @ modem audio rate
                                                   (ifSR / D2, passed with audioReady)
                                                               ↓ emit audioReady()
                                                   FmAudioOutput::push()
                                                     ├─ Linear resample → 48 kHz
                                                     ├─ AGC (target 0.12 RMS)
                                                     ├─ Mono → stereo
                                                     └─ QAudioSink (WASAPI)
```

### Recording pipeline
```
Combined Pipeline → [pool task] RawFileHandler     → combined .cf32
PrePipeline[N]   → [pool task] RawFileHandler      → per-channel .cf32
Combined Pipeline → (via DemodulatorPanel extra handlers)
                      BandpassHandler               → filtered .cf32
                      AudioFileHandler              → .wav (mono float32 PCM)
```

**Filename format:** `{YYYYMMDD}_{HHMMSS}_{source}[_{suffix}]_{centerFreq}_{sampleRate}.{ext}`
(e.g. `20260911_142530_dualrx_fm0_100.300MHz_2.000MSps.wav`)

| Recording | source tag / suffix | ext |
|-----------|---------------------|-----|
| Per-channel I/Q | `rx0` / `rx1` | `.cf32` |
| Combined I/Q | `dualrx` / `triplerx` / `quadrorx` (by channel count) | `.cf32` |
| Filtered (per demod) | `{combined}_bp{BW}kHz` (250 kSps by default) | `.cf32` |
| Audio (per demod) | `{combined}_{mode}{N}` (e.g. `dualrx_fm0`, `dualrx_usb1`) | `.wav` |

### TX path
```
TxController::startTx()
  ├─ device->setFrequency(TX0, freqHz)
  ├─ device->setGain(TX0, gainDb)
  └─ TxWorker::run():
       ITxSource::generateBlock() → float[N×2]
         └─ int16 conversion → IDevice::writeBlock(TX0, buffer, N, timeoutMs)
```

## UI structure

**DeviceSelectionWindow** — lists detected LimeSDR and SoapySDR devices (one button per device) plus an **Open I/Q file...** button for playback. Uses `SessionManager` to prevent opening the same device twice.

**DeviceDetailWindow (4 pages via QListWidget + QStackedWidget):**

| Page | Content |
|------|---------|
| *Device Info* | Serial, name, current sample rate |
| *Device Control* | Init, calibrate, sample rate selector; channel count (1–2) + per-channel gain sliders; optional single-channel assignment combo |
| *Радиомониторинг* | Center freq spin+slider, FFT plot + waterfall, Add demodulator button, Record checkbox + Settings, up to 4 DemodulatorPanels, Start/Stop |
| *Transmit* | TX frequency, TX gain, tone offset + amplitude, Start/Stop TX |

**RadioMonitorPage** (Радиомониторинг page):
- FFT plot with VFO band overlay per DemodulatorPanel, plus a waterfall (`WaterfallView`)
- `+` button adds a DemodulatorPanel (max 4, enforced with warning)
- Record checkbox + gear button opens `RecordingSettingsDialog` (dir, format, raw/filtered/audio toggles)

**DemodulatorPanel** (one per active demodulator slot):
- Mode selector: Off / FM / NFM / AM / SAM / USB / LSB / CW (hot-swap mid-stream)
- VFO freq spinbox (offset from LO); per-mode parameters auto-built from `modem::ParamDesc`
- Volume slider, IF RMS level indicator
- Filtered .cf32 recording checkbox, Audio .wav recording checkbox
- × close button removes the slot

## Planned

- AI signal classifier (ClassifierHandler already wired, Python subprocess via ClassifierController)
- ISyncController integration for dual-LimeSDR clock sync
