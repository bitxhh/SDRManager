#pragma once

#include <mutex>

// ---------------------------------------------------------------------------
// FFTW planner lock — process-wide.
//
// FFTW's planner API (fftwf_plan_dft_1d / fftwf_destroy_plan) is NOT
// thread-safe; only fftwf_execute and fftwf_malloc/free are. The planner is
// global to the process, so every translation unit that creates or destroys
// plans must serialize through this single mutex — a per-file mutex would not
// protect against concurrent planning in another file (e.g. FftProcessor on
// the FFT thread vs WaterfallHandler on a DSP pool thread, both planning on
// the first block after stream start).
// ---------------------------------------------------------------------------
inline std::mutex& fftwPlannerMutex() {
    static std::mutex m;
    return m;
}
