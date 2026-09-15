# DSP Reference

## I/Q format (v2.0)

All `IPipelineHandler::processBlock()` calls receive **float32 interleaved I/Q**
normalised to `[-1, 1]`. Conversion `int16 → float` is done once in `RxWorker`
before the first `PrePipeline` dispatch — no handler ever touches raw int16.

## FM demodulation chain

```
float I/Q → DC blocker (IIR HP) → NCO freq-shift
          → FIR1 LPF (complex, 255 taps, Blackman)  ← push O(1) per sample
          → decimate D1 → IF @ 500 kHz               ← compute O(N) only here
          → FM discriminator (atan2 conjugate product)
          → de-emphasis IIR (τ = 50 µs EU / 75 µs US)
          → FIR2 LPF (real, 255 taps, fc ≈ 15 kHz)
          → decimate D2=10 → audio @ 50 kHz
```

### FM parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| IF target | 500 kHz | D1 = round(inputSR / 500000) |
| Audio SR | 50 kHz | IF / D2 |
| FIR1 taps | 255 (Release) / 31 (Debug) default; per-demod ⚙ dialog, 15–1023 odd | -55 dB at Nyquist |
| FIR1 bandwidth | 150 kHz default | Adjustable 50–225 kHz |
| FIR2 taps | 255 | Rejects FM stereo subcarrier (23–53 kHz) |
| FM max deviation | ±75 kHz | demodGain = ifSR / (2π × 75000) |
| De-emphasis | 75 µs US default | fc ≈ 2122 Hz |

## AM demodulation chain

```
float I/Q → DC blocker (IIR HP) → NCO freq-shift
          → FIR1 LPF (complex, 255 taps)
          → decimate D1 → IF @ 500 kHz
          → envelope: sqrt(I² + Q²)
          → DC removal (IIR HP ~20 Hz)
          → FIR2 LPF (real, 255 taps, fc ≈ 5 kHz)
          → decimate D2=10 → audio @ 50 kHz
```

### AM parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| IF target | 500 kHz | Same IF architecture as FM |
| Audio SR | 50 kHz | Same FmAudioOutput path |
| FIR1 bandwidth | 5 kHz default | Adjustable 1–20 kHz |
| FIR2 cutoff | ~5 kHz | Matches AM bandwidth |
| DC removal | IIR HP ~20 Hz | Removes carrier DC after sqrt() |

## NFM demodulation chain

Narrowband FM for voice channels (PMR/amateur ~12.5 kHz). Same discriminator as
WBFM but a narrow channel FIR1, a 4 kHz voice FIR2, and **no de-emphasis**.

```
float I/Q → DC blocker (IIR HP) → NCO freq-shift
          → FIR1 LPF (complex, 255 taps, fc = bandwidth/2)
          → decimate D1 → IF @ 500 kHz
          → FM discriminator (atan2 conjugate product), gain = ifSR / (2π·maxDev)
          → FIR2 LPF (real, 255 taps, fc ≈ 4 kHz voice)
          → decimate D2=10 → audio @ 50 kHz
```

Overrides `demodulateIF()` only (default `produceAudio` path).

### NFM parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Channel bandwidth | 12.5 kHz default | Clamp 6 kHz – 0.9·ifSR; FIR1 fc = BW/2 |
| Max deviation | ±5 kHz default | demodGain = ifSR / (2π·maxDev); clamp 1–15 kHz |
| FIR2 cutoff | 4 kHz | Voice audio; no stereo/de-emphasis |
| Min IF | 100 kHz | Throws below this device SR |

## SSB demodulation chain (USB / LSB)

One parameterized modem — `sideband = +1` (USB) or `−1` (LSB). Phasing (Hilbert)
method. `produceAudio` is fully overridden: a complex channel-select decimator
replaces the FIR1→FIR2 real path.

```
float I/Q → DC blocker → NCO freq-shift
          → FIR1 LPF (complex, 100 kHz wide anti-alias)
          → decimate D1 → IF @ 500 kHz
          → channel FIR (complex, 255 taps, fc = bandwidth) + decimate → audio @ 50 kHz
          → Hilbert(Q) (127-tap Type III)  vs  I delayed by (127−1)/2
          → audio = 0.5·(I_delayed − sideband·Hilbert(Q))
```

USB uses `I − H{Q}`, LSB uses `I + H{Q}`; the 0.5 undoes the phasing 2× gain.
The integer group delay of the Type III Hilbert is matched by a `DelayLine` on I.

### SSB parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Bandwidth | 2.8 kHz default | Clamp 1 kHz – 0.9·(audioSR/2) |
| Sideband | +1 USB / −1 LSB | Set at construction (`UsbModem`/`LsbModem`) |
| Channel FIR | 255 taps @ IF | Sharp SSB skirt, fc = BW |
| Hilbert FIR | 127 taps (odd, Type III) | Integer group delay = 63 |
| Min IF | 20 kHz | |

## CW demodulation chain (Morse)

Narrow bandpass with an audible BFO sidetone. Like SSB, `produceAudio` is
overridden with a complex channel decimator, then a BFO NCO beats DC → pitch.

```
float I/Q → DC blocker → NCO freq-shift
          → FIR1 LPF (complex, 100 kHz wide anti-alias)
          → decimate D1 → IF @ 500 kHz
          → channel FIR (complex, 255 taps, fc = bandwidth/2) + decimate → audio @ 50 kHz
          → BFO NCO mix (shift DC → pitchHz), take real part
          → audio @ 50 kHz
```

An on-frequency carrier (key-down) lands at DC after the NCO, so the BFO turns it
into a clean beat note at exactly the pitch frequency.

### CW parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Bandwidth | 500 Hz default | Clamp 50 Hz – 0.9·(audioSR/2); FIR fc = BW/2 |
| Pitch (BFO) | 700 Hz default | Clamp 300–1200 Hz; sidetone frequency |
| Channel FIR | 255 taps @ IF | Steep skirt for CW selectivity |
| Min IF | 20 kHz | |

## SAM demodulation chain (Synchronous AM)

Carrier-tracking PLL synchronous detector — a phase-locked reference derotates the
carrier so the real part is the recovered envelope, without the distortion of an
envelope detector at low signal levels. Overrides `demodulateIF()` only (default
`produceAudio` path with FIR2 + D2 decimation).

```
float I/Q → DC blocker → NCO freq-shift
          → FIR1 LPF (complex, 100 kHz wide anti-alias)
          → decimate D1 → IF @ 500 kHz
          → carrier PLL: derotate to baseband, take real part
          → audio DC removal (IIR HP ~20 Hz, removes carrier DC term)
          → FIR2 LPF (real, 255 taps, fc = bandwidth)
          → decimate D2=10 → audio @ 50 kHz
```

The PLL frequency term is clamped to a ±1 kHz pull-in range so it locks onto the
carrier rather than a modulation sideband.

### SAM parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Bandwidth | 5 kHz default | Clamp 1 kHz – 0.9·(audioSR/2); sets FIR2 |
| PLL loop bandwidth | 100 Hz default | Clamp 10–500 Hz |
| Pull-in range | ±1 kHz | PLL freq clamp, locks carrier not sideband |
| DC removal | IIR HP ~20 Hz | Removes carrier DC after synchronous detection |
| Min IF | 20 kHz | |

## DSP building blocks (`DspUtils`)

Shared primitives used by the demodulators above (all unit-tested in
`test_dsputils.cpp`):

| Helper | Purpose |
|--------|---------|
| `designLowpassFir(N, fcNorm)` | Windowed-sinc lowpass (Blackman), real taps |
| `designBandpassFir(N, f1, f2)` | Windowed-sinc bandpass |
| `designHilbertFir(N)` | Type III Hilbert transformer (odd N, antisymmetric) |
| `FirComplexDecimator` | Complex FIR + integer decimation (SSB/CW channel select) |
| `DelayLine` | Integer-sample delay (matches Hilbert group delay) |
| `CarrierPll` | Second-order carrier-tracking PLL (SAM synchronous detect) |
| `Nco` | Numerically-controlled oscillator (station offset + CW BFO) |

## Why 500 kHz IF

FIR1 must anti-alias before D1 decimation. Transition band = Nyquist − passband:
- 250 kHz IF: transition = 125 − 100 = 25 kHz → ~1000 taps (impractical)
- 500 kHz IF: transition = 250 − 150 = 100 kHz → 255 taps give ~-55 dB

## FIR1 push/compute optimization

Dot product computed only at decimation output points (every D1-th sample).
Between points: O(1) push into delay line. Gives D1× speedup (8× at 4 MS/s).

## FFT (FftProcessor)

Single-precision FFTW3, AVX2+FMA path. Thread-local plan cache (one plan per thread
reused across blocks). ~2× throughput improvement vs double-precision (measured on
Ryzen with AVX2).

## IqCombiner — coherent channel combining

Both RX channels on LimeSDR share one RXPLL (same LO) → coherent I/Q.

```
CH0 block (float32) ──┐
CH1 block (float32) ──┴─ gain normalise each channel (÷ linear gain)
                           ─ sample-by-sample average
                           → combined block → Combined Pipeline
```

Gain normalisation: `scale_n = 1 / 10^(gainDb_n / 20)`.  
Averaging N coherent channels: noise averages down by √N in amplitude → +3 dB SNR per doubling.  
2 channels: +3 dB spectrum display, up to +6 dB demodulator SNR (pre-detection combining).

Timestamp matching uses `BlockMeta::timestamp` (hardware sample counter). If timestamps
differ by more than one block, the older slot is dropped and a new one is waited for.

## BandpassHandler — per-demodulator filtered recording

```
Combined I/Q → NCO shift to VFO offset
             → complex FIR LPF (fc = BW/2)
             → decimate → float32 .cf32 file
```

Written via `BandpassExporter`; output sample rate = inputSR / decimation factor.

## AudioFileHandler — WAV recording

Receives `audioReady(QVector<float>, double sampleRateHz)` from `ModemHandler`.
Writes RIFF/WAVE IEEE-float PCM mono. Sample rate is locked at first block; the WAV
header is patched (seek back) on `close()` with the final sample count.

Filename is determined by a `PathBuilder` closure passed at construction — the closure
receives the audio SR (known only at first block) and composes the path using `FileNaming`.

## Supported sample rates

`{2.5, 4, 5, 8, 10, 15, 20}` MS/s — all yield integer D1 for 500 kHz IF.  
Debug build max: 10 MS/s (31-tap FIR1 — insufficient selectivity above that).  
Release build: 255-tap FIR1 — all rates viable.
