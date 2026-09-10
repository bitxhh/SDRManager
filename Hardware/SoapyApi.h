#pragma once

#include <cstddef>

// ---------------------------------------------------------------------------
// SoapyApi — динамическая загрузка C API SoapySDR (SoapySDR.dll, PothosSDR).
//
// DLL собрана MSVC, приложение — MinGW, поэтому C++ API SoapySDR использовать
// нельзя (несовместимый ABI). C API стабилен: типы ниже продекларированы
// вручную (POD, layout совпадает с SoapySDR 0.8), функции резолвятся через
// QLibrary в рантайме. Если DLL не найдена — loaded == false и
// SoapyDeviceManager просто не находит устройств.
//
// ВАЖНО: память, выделенная внутри DLL (kwargs-списки, массивы rate'ов),
// освобождается только функциями самой DLL (разные CRT у MSVC и MinGW).
// ---------------------------------------------------------------------------

// ── Минимальные C-типы SoapySDR 0.8 (ABI-совместимые POD) ────────────────────
struct SoapySDRKwargs {
    size_t size;
    char** keys;
    char** vals;
};

struct SoapySDRRange {
    double minimum;
    double maximum;
    double step;
};

struct SoapySDRDevice;   // opaque
struct SoapySDRStream;   // opaque

namespace soapy {

// Направления (SoapySDR/Constants.h)
inline constexpr int kTx = 0;
inline constexpr int kRx = 1;

// Коды ошибок readStream (SoapySDR/Errors.h)
inline constexpr int kErrTimeout      = -1;
inline constexpr int kErrStreamError  = -2;
inline constexpr int kErrCorruption   = -3;
inline constexpr int kErrOverflow     = -4;
inline constexpr int kErrNotSupported = -5;
inline constexpr int kErrTimeError    = -6;
inline constexpr int kErrUnderflow    = -7;

// ── Таблица указателей на функции C API ──────────────────────────────────────
struct Api {
    // enumeration / lifecycle
    SoapySDRKwargs* (*enumerateStrArgs)(const char* args, size_t* length){};
    SoapySDRDevice* (*makeStrArgs)(const char* args){};
    int (*unmake)(SoapySDRDevice* device){};
    const char* (*lastError)(){};

    // parameters
    int    (*setSampleRate)(SoapySDRDevice*, int dir, size_t ch, double rate){};
    double (*getSampleRate)(const SoapySDRDevice*, int dir, size_t ch){};
    double* (*listSampleRates)(const SoapySDRDevice*, int dir, size_t ch, size_t* length){};
    int    (*setFrequency)(SoapySDRDevice*, int dir, size_t ch, double hz,
                           const SoapySDRKwargs* args){};
    double (*getFrequency)(const SoapySDRDevice*, int dir, size_t ch){};
    int    (*setGain)(SoapySDRDevice*, int dir, size_t ch, double dB){};
    double (*getGain)(const SoapySDRDevice*, int dir, size_t ch){};
    int    (*setGainMode)(SoapySDRDevice*, int dir, size_t ch, bool automatic){};
    SoapySDRRange (*getGainRange)(const SoapySDRDevice*, int dir, size_t ch){};

    // streaming (сигнатура 0.8: setupStream возвращает указатель на стрим)
    SoapySDRStream* (*setupStream)(SoapySDRDevice*, int dir, const char* format,
                                   const size_t* channels, size_t numChans,
                                   const SoapySDRKwargs* args){};
    int (*closeStream)(SoapySDRDevice*, SoapySDRStream*){};
    int (*activateStream)(SoapySDRDevice*, SoapySDRStream*, int flags,
                          long long timeNs, size_t numElems){};
    int (*deactivateStream)(SoapySDRDevice*, SoapySDRStream*, int flags,
                            long long timeNs){};
    int (*readStream)(SoapySDRDevice*, SoapySDRStream*, void* const* buffs,
                      size_t numElems, int* flags, long long* timeNs,
                      long timeoutUs){};

    // memory helpers
    void (*kwargsListClear)(SoapySDRKwargs* args, size_t length){};
    const char* (*kwargsGet)(const SoapySDRKwargs* args, const char* key){};
    void (*sdrFree)(void* ptr){};   // опционально: может отсутствовать в старых DLL

    bool loaded{false};
};

// Единственный экземпляр; загрузка происходит при первом обращении.
// Потокобезопасно (magic static). DLL остаётся загруженной до конца процесса.
const Api& api();

} // namespace soapy
