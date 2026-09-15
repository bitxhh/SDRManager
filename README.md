# SDRManager

**English** | [Русский](README.ru.md)

Qt6/C++ SDR receiver for LimeSDR and SoapySDR devices — real-time spectrum & waterfall,
FM/NFM/AM/SAM/SSB/CW demodulation, coherent dual-channel combining, I/Q recording and playback.

<!-- Screenshots: coming soon -->

## Features

- **Multiple backends**
  - LimeSDR via LimeSuite (both RX channels + TX)
  - any SoapySDR device (RTL-SDR, HackRF, Airspy, …), loaded at runtime
  - playback of recorded I/Q files (`.cf32` / `.cs16`)
- **Real-time spectrum and waterfall**, with EMA smoothing, zoom and a dark plot theme
- **Up to 4 independent demodulators** on one stream, each with its own VFO, bandwidth, volume and recording
- **Modes**: WBFM, NFM, AM, SAM (synchronous AM), USB, LSB, CW. Modes can be switched while streaming.
- **Coherent dual-channel combining** of LimeSDR RX0 + RX1, with automatic phase calibration
- **Recording**
  - raw I/Q (`.cf32`), per channel or combined
  - band-filtered I/Q, per demodulator
  - demodulated audio (`.wav`)
- **Per-device settings persistence**: sample rate, gains, frequency and demodulator panels are restored on the next launch
- **Transmit test tone**: a TX0 sinusoid generator
- **Optional AI modulation classifier**: a Python service connected over a local TCP socket (`Python/classifier_service.py`)

## Architecture

```
DeviceManager (LimeSDR / SoapySDR / I/Q file)
      │
      ▼
RxWorker (QThread, one per RX channel) ──► PrePipeline ──► IqCombiner
                                                              │
                                                              ▼
                                               Combined Pipeline (QThreadPool)
                     ┌──────────────┬──────────────────┬──────┴─────────┬──────────────────┐
                     ▼              ▼                  ▼                ▼                  ▼
                FftHandler   WaterfallHandler   ModemHandler ×N    RawFileHandler   Bandpass / Audio
                 spectrum        waterfall        demodulator         → .cf32        file handlers
                                                       │
                                                       ▼
                                                 FmAudioOutput
                                            resample + AGC → WASAPI
```

The UI thread only renders and sends commands. I/Q reception runs on dedicated `QThread`s,
and signal processing handlers run in parallel on a shared thread pool.
See [docs/architecture.md](docs/architecture.md) for details.

## Build

### Requirements

- **Windows** + **MinGW** (GCC 13+, as shipped with Qt)
- **CMake** 3.16+
- **Qt 6.10**: Widgets, Concurrent, PrintSupport, Multimedia, Network
- **LimeSuite**: headers + `LimeSuite.dll` at `C:/LimeSuite`
- **SoapySDR** *(optional, runtime)*: `SoapySDR.dll` on `PATH` or in `C:/Program Files/PothosSDR/bin`.
  Without it, SoapySDR devices are simply not listed.
- **CPU with AVX2 + FMA**: the build uses `-mavx2 -mfma`
- Bundled in `external/`: **FFTW3** (single precision) and **QCustomPlot**

The Qt and LimeSuite paths are set at the top of `CMakeLists.txt`. Adjust them if yours differ.

### Build commands

```bash
# Release (recommended)
cmake -S . -B cmake-build-release-mingw-qt -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release-mingw-qt --target SDRManager
```

Use **Release** for listening. Debug builds use a shorter FIR and can't sustain ≥15 MS/s.
The post-build step runs `windeployqt` and copies `LimeSuite.dll` and `libfftw3f-3.dll` next to the executable.

### Tests

```bash
cmake --build cmake-build-release-mingw-qt --target SDRManagerTests
ctest --test-dir cmake-build-release-mingw-qt
```

Catch2 unit tests cover the FFT, IqCombiner, DSP helpers and every demodulator (FM, NFM, AM, SAM, SSB, CW).

### Run

1. Launch `SDRManager.exe`.
2. Pick a device, or use **Open I/Q file...** to play back a recording.
3. Initialize the device, set the sample rate, calibrate, and start streaming.

Settings and the log (`sdrmanager.log`) are stored in `%APPDATA%\SDRManager`.

## Hardware configuration (LimeSDR)

The LimeSDR is configured to match **ExtIO_LimeSDR** (the HDSDR plugin), a known-good reference:

- Analog LPF bandwidth = sample rate
- TIA is protected around `LMS_SetLPFBW` calls (LimeSuite bug workaround)
- The PGA compensation register (`RCC_CTL_PGA_RBB`) is updated on every gain change
- Calibration bandwidth = max(sample rate, 2.5 MHz)

More details are in [docs/hardware.md](docs/hardware.md).

## Project structure

```
Core/           Interfaces and infrastructure (IDevice, IPipelineHandler, Pipeline, Logger, settings)
Hardware/       Device backends (LimeSDR, SoapySDR, I/Q file), RX/TX workers
DSP/            FFT, waterfall, modems, IqCombiner, recorders
Audio/          Audio output (resampler, AGC, QAudioSink)
Application/    Qt UI (device selection, radio monitor, demodulator panels, TX)
Tests/          Unit tests (Catch2)
Python/         Optional modulation classifier service
external/       Bundled dependencies (FFTW, QCustomPlot)
docs/           Architecture, DSP chains, hardware notes
```

## Documentation

- [docs/architecture.md](docs/architecture.md): components, threading model, interfaces, data flow
- [docs/dsp.md](docs/dsp.md): DSP chains and parameters of every modem
- [docs/hardware.md](docs/hardware.md): LimeSDR init sequence, hardware quirks, gain structure

## License

The source code of this project is released under the [MIT License](LICENSE).

Note that some third-party dependencies use copyleft licenses:
- QCustomPlot: GPL-3.0
- FFTW: GPL-2.0+
- Qt: LGPL-3.0 / GPL

A distributed binary that links them must follow their terms. The other dependencies are permissive:
LimeSuite is Apache-2.0 and SoapySDR is Boost.
