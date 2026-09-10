#pragma once

#include <QSettings>
#include <algorithm>

// ---------------------------------------------------------------------------
// WaterfallSettings — конфигурация водопада (страница Радиомониторинг).
//
// Persisted globally in QSettings under "waterfall/*" (same pattern as the
// "recording/*" keys) — display preference, not a per-device setting.
// ---------------------------------------------------------------------------
struct WaterfallSettings {
    enum class Colormap    { ClassicSdr = 0, Viridis, Inferno, Turbo, Grayscale };
    enum class Aggregation { MaxHold = 0, Average };

    bool        enabled      = true;
    int         fftSize      = 4096;                  // 512 … 16384, power of two
    int         fps          = 25;                    // waterfall lines per second
    Colormap    colormap     = Colormap::ClassicSdr;
    Aggregation aggregation  = Aggregation::MaxHold;  // how the FFTs of one line collapse
    double      dbMin        = -110.0;                // colormap floor
    double      dbMax        = -30.0;                 // colormap ceiling
    int         historyDepth = 1200;                  // lines kept for re-render / tall windows

    [[nodiscard]] static WaterfallSettings load();
    void save() const;
};

inline WaterfallSettings WaterfallSettings::load() {
    QSettings s;
    WaterfallSettings w;
    w.enabled      = s.value("waterfall/enabled",      w.enabled).toBool();
    w.fftSize      = s.value("waterfall/fftSize",      w.fftSize).toInt();
    w.fps          = s.value("waterfall/fps",          w.fps).toInt();
    w.colormap     = static_cast<Colormap>(
                     s.value("waterfall/colormap",     int(w.colormap)).toInt());
    w.aggregation  = static_cast<Aggregation>(
                     s.value("waterfall/aggregation",  int(w.aggregation)).toInt());
    w.dbMin        = s.value("waterfall/dbMin",        w.dbMin).toDouble();
    w.dbMax        = s.value("waterfall/dbMax",        w.dbMax).toDouble();
    w.historyDepth = s.value("waterfall/historyDepth", w.historyDepth).toInt();

    // Sanitize — the settings file may come from an older version or be hand-edited.
    const bool pow2 = w.fftSize >= 512 && w.fftSize <= 16384
                      && (w.fftSize & (w.fftSize - 1)) == 0;
    if (!pow2) w.fftSize = 4096;
    w.fps          = std::clamp(w.fps, 5, 60);
    w.historyDepth = std::clamp(w.historyDepth, 200, 4000);
    if (int(w.colormap) < 0 || int(w.colormap) > int(Colormap::Grayscale))
        w.colormap = Colormap::ClassicSdr;
    if (w.aggregation != Aggregation::MaxHold && w.aggregation != Aggregation::Average)
        w.aggregation = Aggregation::MaxHold;
    if (w.dbMax - w.dbMin < 5.0) { w.dbMin = -110.0; w.dbMax = -30.0; }
    return w;
}

inline void WaterfallSettings::save() const {
    QSettings s;
    s.setValue("waterfall/enabled",      enabled);
    s.setValue("waterfall/fftSize",      fftSize);
    s.setValue("waterfall/fps",          fps);
    s.setValue("waterfall/colormap",     int(colormap));
    s.setValue("waterfall/aggregation",  int(aggregation));
    s.setValue("waterfall/dbMin",        dbMin);
    s.setValue("waterfall/dbMax",        dbMax);
    s.setValue("waterfall/historyDepth", historyDepth);
}
