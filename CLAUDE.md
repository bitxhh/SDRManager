# CLAUDE.md

SDRManager — SDR receiver for LimeSDR and SoapySDR devices. Real-time spectrum & waterfall, FM/NFM/AM/SAM/SSB/CW demodulation, multi-channel coherent combining, I/Q recording/playback, WAV export.

## Build

CMake + MinGW + Qt6.

```bash
cmake --build cmake-build-release-mingw-qt --target SDRManager
```

Use **Release** for FM listening — Debug uses 31-tap FIR1 and can't sustain ≥15 MS/s.

## Dependencies

| Dependency | Location |
|------------|----------|
| Qt 6.10 | `C:/Qt/6.10.0/mingw_64` — Widgets, Concurrent, PrintSupport, Multimedia, Network |
| LimeSuite | `C:/LimeSuite` — headers + `LimeSuite.dll` |
| SoapySDR (optional) | runtime-loaded via `QLibrary`; `PATH` or `C:/Program Files/PothosSDR/bin` |
| FFTW3 | `external/FFTW/` — single precision (float32), static |
| QCustomPlot | `external/qcustomplot/` — static lib |
| Catch2 v3.7.1 | `Tests/` — unit tests for FFT, IqCombiner, DSP helpers, and all demodulators (FM, AM, NFM, SSB, CW, SAM) |

## Quick reference

- Architecture, threading, interfaces, data flow → `docs/architecture.md`
- DSP chains and parameters for all modems → `docs/dsp.md`
- LimeSDR/SoapySDR init, hardware quirks, gain structure → `docs/hardware.md`
- Load all docs in conversation → `/sdrmanager-docs`

For all hardware questions, refer to the information at the link https://wiki.myriadrf.org/LimeMicro:LMS7002M_Datasheet#Data_converters_clock_generation

For all LimeSuite questions, refer to the information at the link https://deepwiki.com/myriadrf/LimeSuite/1-limesuite-overview
