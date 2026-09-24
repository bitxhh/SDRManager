# DSP Reference

## I/Q format (v2.0)

All `IPipelineHandler::processBlock()` calls receive **float32 interleaved I/Q**
normalised to `[-1, 1]`. Conversion `int16 → float` is done once in `RxWorker`
before the first `PrePipeline` dispatch — no handler ever touches raw int16.

## FM demodulation chain

```
float I/Q → DC blocker (IIR HP) → NCO freq-shift
          → halfband ÷2 cascade (47 taps, only when D1 has factors of 2)
          → FIR1 LPF (complex, 255 taps, Blackman)  ← push O(1) per sample
          → decimate D1 → IF @ 480/500 kHz           ← compute O(N) only here
          → FM discriminator (atan2 conjugate product)
          → de-emphasis IIR (τ = 50 µs EU / 75 µs US)
          → FIR2 LPF (real, 255 taps, fc ≈ 15 kHz)  ← dot product only on outputs
          → decimate D2=10 → audio @ 48/50 kHz
```

### FM parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| IF target | 480 kHz if inputSR is a multiple of 480k, else ~500 kHz | D1 = inputSR/480000 or round(inputSR / 500000); factors of 2 go to halfband stages while FIR1 keeps ≥ ÷2 |
| Audio SR | 48 kHz (exact, no resampling) or ~50 kHz | IF / D2; FmAudioOutput resamples to the sink rate with a 16-tap windowed-sinc polyphase resampler |
| FIR1 taps | 255 (Release) / 31 (Debug) default; per-demod ⚙ dialog, 15–1023 odd | -55 dB at Nyquist |
| FIR1 bandwidth | ±100 kHz default (one-sided cutoff; the UI draws ±bw) | Adjustable 30 kHz – 0.9·IF/2. ±90–100 kHz ≈ Carson (75k dev + 15k audio). Wider bw with a strong neighbour ±200–300 kHz away lets it into the discriminator (capture → heavy noise); below ~80 kHz a sharp FIR1 cuts the FM spectrum (distortion). FIR1 cutoff is always clamped to 0.95·IF/2 (constructor and setBandwidth) to prevent adjacent-channel aliasing |
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
          → [audio chain] DcsDetector → CtcssDetector (NfmModemHandler::buildAudioChain)
```

Overrides `demodulateIF()` only (default `produceAudio` path).

**CTCSS** (`DSP/CtcssDetector`, audio-chain stage added by `NfmModemHandler`):

```
audio → FIR LPF 400 Hz, evaluated only at decimation points → ~2 kHz
      → Goertzel bank, 50 EIA tones 67.0–254.1 Hz, 0.4 s window (2.5 Hz bins)
      → best bin ≥ 30 % of DC-removed window energy → detected tone
        (held through one missed window, cleared after two)
audio → [mute if CTCSS target ≠ 0 and |detected − target| > 1 Hz]
      → 6th-order Butterworth HPF 300 Hz (3 RBJ biquads) — the tone is not heard
```

`NfmModemHandler::ctcssToneChanged(hz)` (worker thread) → «CTCSS: xx.x Hz» label
in DemodulatorPanel. Tone squelch works on top of the page-level squelch.

**DCS** (`DSP/DcsDetector`, audio-chain stage before CtcssDetector — the CTCSS
300 Hz HPF would remove the DCS band):

```
audio → FIR LPF 300 Hz, evaluated only at decimation points → ~1344 Hz (10×134.4)
      → subtract one-word (23-bit) moving average — exact DC of the NRZ stream
      → slicer → integrate-and-dump bits, bit clock pulled to slicer edges (gain 0.1)
      → 23-bit window vs all 23 rotations of the 104 standard codes, ≤ 2 bit errors,
        confirmed by the previous word matching the same rotation
      → detected code, held 0.5 s without confirmation
audio → [mute if DCS target ≠ 0 and detected ∉ {target, invertedAlias(target)}]
```

Codeword: Golay (23,12), data `0x800 | code`, generator 0xC75, sent LSB first at
134.4 bit/s. The complement of a codeword is a rotation of another one, so every
inverted code is on air identical to a normal one (023I ≡ 047N); the detector
reports the normal code, and the target also opens on its alias.
`NfmModemHandler::dcsCodeChanged(code)` → «DCS: 047N = 023I» label in DemodulatorPanel.

### NFM parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Channel bandwidth | 12.5 kHz default | Clamp 6 kHz – 0.9·ifSR; FIR1 fc = BW/2 |
| Max deviation | ±5 kHz default | demodGain = ifSR / (2π·maxDev); clamp 1–15 kHz |
| FIR2 cutoff | 4 kHz | Voice audio; no stereo/de-emphasis |
| Min IF | 100 kHz | Throws below this device SR |
| CTCSS | Off (default) / 67.0–254.1 Hz | Combo, 51 options; tone squelch target, detection runs always |
| DCS | Off (default) / 023–754 | Combo, 105 options, value = code as octal number; code squelch target, detection runs always |

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

## Common stages (all modems)

### Impulse noise blanker (I/Q)

`dsp::NoiseBlanker` runs in `ChannelModem::pushBlock` right after the DC
blocker, at the full input rate. At that point impulses from ignition,
switching supplies and similar sources are still a few samples long; the
halfbands and FIR1 would otherwise stretch them to the filter length.

- The detector compares |x|² with an exponential moving average (τ = 5 ms).
  The average is fed `min(|x|², threshold × avg)`, so impulses cannot pull it
  up. For the first τ it is a plain running mean and does not trigger.
- A sample above `threshold × avg` blanks the signal for `width` µs. Every
  further hit re-arms the window.
- The output goes through a 2 µs look-ahead delay. The gain fades to 0 over
  those 2 µs and reaches 0 exactly when the impulse leaves the delay line,
  then fades back in. This avoids the clicks of hard gating.
- `threshold ≤ 0` disables the blanker. It is then a pure bypass with no
  delay, and this is the default.

| Param (`ModemHandler::setParam`) | Default | Meaning |
|------|---------|---------|
| `NB Threshold` (`kNbThresholdKey`) | 0 (off) | Trigger level, × mean power (typical 5–20) |
| `NB Width` (`kNbWidthKey`) | 20 µs | Blanking window after the last hit |

The test `test_noiseblanker.cpp` uses a 50 kHz tone with 3-sample impulses
every 1 ms at 2 MS/s:

- the blanker improves SNR from −5.7 dB to +16.5 dB;
- a clean tone passes as a pure 4-sample delay with no blanking.

### Audio post-processing chain (`IAudioProcessor`)

`DSP/AudioProcessor.h` defines this interface for post-demod stages such as
NR, notch or AGC. They run after FIR2 / ÷D2, in place, in insertion order, on
every block that `pushBlock()` returns.

| Method | When |
|--------|------|
| `prepare(audioSR)` | Once, from `ChannelModem::addAudioProcessor()` |
| `process(float*, n)` | Every audio block, on the RxWorker thread |
| `reset()` | On a station change (`ChannelModem::setOffset`) |
| `setParam(name, v)` | Live parameter. Returns `true` if the stage owns the name |

How a handler wires this up:

- A handler adds its stages in `ModemHandler::buildAudioChain()`, which is
  called on every (re)build of the demodulator, including a taps change.
  After that, the whole param snapshot is replayed into the new chain.
- For a live change, `ModemHandler::processBlock` calls
  `ChannelModem::setCommonParam()` first. That call handles the noise-blanker
  params and then offers the name to the audio chain. Only names nobody
  claims reach the per-modem `applyParam()`.

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
| `NoiseBlanker` | Impulse blanker on I/Q: power detector, look-ahead fade (tested in `test_noiseblanker.cpp`) |

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
