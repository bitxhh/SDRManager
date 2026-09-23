#include <catch2/catch_test_macros.hpp>

#include "Pipeline.h"
#include "LoggerConfig.h"

#include <QThreadPool>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

// Records every call; safe to hit from pool threads.
class RecordingHandler : public IPipelineHandler {
public:
    void processBlock(const float* iq, int count, double sr) override {
        ++blocks;
        lastIq    = iq;
        lastCount = count;
        lastSr    = sr;
        threadId  = std::this_thread::get_id();
    }
    void processBlock(const float* iq, int count, double sr, const BlockMeta& meta) override {
        ++metaBlocks;
        lastMeta = meta;
        processBlock(iq, count, sr);
    }
    void onStreamStarted(double sr) override { ++started; startedSr = sr; }
    void onStreamStopped() override          { ++stopped; }
    void onRetune(double f) override         { ++retunes; retuneHz = f; }

    std::atomic<int> blocks{0}, metaBlocks{0}, started{0}, stopped{0}, retunes{0};
    const float*     lastIq{nullptr};
    int              lastCount{0};
    double           lastSr{0.0}, startedSr{0.0}, retuneHz{0.0};
    BlockMeta        lastMeta{};
    std::thread::id  threadId{};
};

// Only overrides the pure-virtual overload → exercises the default meta forwarding.
class PlainHandler : public IPipelineHandler {
public:
    void processBlock(const float*, int count, double) override { total += count; }
    int total{0};
};

// Blocks until `expected` handlers are inside processBlock simultaneously.
// Succeeds only if dispatch actually runs handlers in parallel.
class RendezvousHandler : public IPipelineHandler {
public:
    RendezvousHandler(std::atomic<int>& arrived, int expected)
        : arrived_(arrived), expected_(expected) {}
    void processBlock(const float*, int, double) override {
        ++arrived_;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (arrived_.load() < expected_ && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        metAll = arrived_.load() >= expected_;
        done   = true;
    }
    std::atomic<bool> metAll{false}, done{false};
private:
    std::atomic<int>& arrived_;
    int               expected_;
};

// Sets the pipeline_timing flag for the scope and restores it afterwards
// (setEnabled only touches memory, nothing is written to disk).
class ScopedTimingFlag {
public:
    explicit ScopedTimingFlag(bool on)
        : prev_(LoggerConfig::instance().isEnabled(QLatin1String(LogCat::kPipelineTiming))) {
        LoggerConfig::instance().setEnabled(QLatin1String(LogCat::kPipelineTiming), on);
    }
    ~ScopedTimingFlag() {
        LoggerConfig::instance().setEnabled(QLatin1String(LogCat::kPipelineTiming), prev_);
    }
private:
    bool prev_;
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Synchronous dispatch (no pool)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline without pool dispatches synchronously to all handlers", "[pipeline]") {
    ScopedTimingFlag timing(false);
    Pipeline p;
    RecordingHandler a, b;
    p.addHandler(&a);
    p.addHandler(&b);

    std::vector<float> iq(2 * 128, 0.0f);
    p.dispatchBlock(iq.data(), 128, 2.0e6);

    for (auto* h : {&a, &b}) {
        CHECK(h->blocks == 1);
        CHECK(h->lastIq == iq.data());
        CHECK(h->lastCount == 128);
        CHECK(h->lastSr == 2.0e6);
        CHECK(h->threadId == std::this_thread::get_id());
    }
}

TEST_CASE("Pipeline forwards BlockMeta to the meta overload", "[pipeline]") {
    ScopedTimingFlag timing(false);
    Pipeline p;
    RecordingHandler rec;
    PlainHandler plain;
    p.addHandler(&rec);
    p.addHandler(&plain);

    BlockMeta meta;
    meta.channel   = {ChannelDescriptor::RX, 1};
    meta.timestamp = 123456789ULL;

    std::vector<float> iq(2 * 64, 0.0f);
    p.dispatchBlock(iq.data(), 64, 1.0e6, meta);

    CHECK(rec.metaBlocks == 1);
    CHECK(rec.blocks == 1);
    CHECK(rec.lastMeta.channel == meta.channel);
    CHECK(rec.lastMeta.timestamp == meta.timestamp);
    CHECK(plain.total == 64);   // default overload falls back to processBlock(iq,count,sr)
}

TEST_CASE("Pipeline notifications fan out to every handler", "[pipeline]") {
    ScopedTimingFlag timing(false);
    Pipeline p;
    RecordingHandler a, b;
    p.addHandler(&a);
    p.addHandler(&b);

    p.notifyStarted(10.0e6);
    p.notifyRetune(433.92e6);
    p.notifyStopped();

    for (auto* h : {&a, &b}) {
        CHECK(h->started == 1);
        CHECK(h->startedSr == 10.0e6);
        CHECK(h->retunes == 1);
        CHECK(h->retuneHz == 433.92e6);
        CHECK(h->stopped == 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Handler list management
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline removeHandler and clearHandlers stop delivery", "[pipeline]") {
    ScopedTimingFlag timing(false);
    Pipeline p;
    RecordingHandler a, b;
    p.addHandler(&a);
    p.addHandler(&b);

    std::vector<float> iq(2 * 16, 0.0f);
    p.removeHandler(&a);
    p.dispatchBlock(iq.data(), 16, 1.0e6);
    CHECK(a.blocks == 0);
    CHECK(b.blocks == 1);

    p.removeHandler(&a);   // removing an absent handler is a no-op
    p.dispatchBlock(iq.data(), 16, 1.0e6);
    CHECK(b.blocks == 2);

    p.clearHandlers();
    p.dispatchBlock(iq.data(), 16, 1.0e6);
    p.notifyStarted(1.0e6);
    CHECK(b.blocks == 2);
    CHECK(b.started == 0);
}

TEST_CASE("Pipeline with no handlers is a harmless no-op", "[pipeline]") {
    QThreadPool pool;
    Pipeline p(&pool);
    std::vector<float> iq(2 * 16, 0.0f);
    p.dispatchBlock(iq.data(), 16, 1.0e6);
    p.notifyStarted(1.0e6);
    p.notifyRetune(100.0e6);
    p.notifyStopped();
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Parallel dispatch via QThreadPool
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline with pool runs handlers concurrently and waits for all", "[pipeline][threads]") {
    ScopedTimingFlag timing(false);
    constexpr int kHandlers = 3;

    QThreadPool pool;
    pool.setMaxThreadCount(kHandlers);
    Pipeline p(&pool);

    std::atomic<int> arrived{0};
    std::vector<std::unique_ptr<RendezvousHandler>> hs;
    for (int i = 0; i < kHandlers; ++i) {
        hs.push_back(std::make_unique<RendezvousHandler>(arrived, kHandlers));
        p.addHandler(hs.back().get());
    }

    std::vector<float> iq(2 * 32, 0.0f);
    p.dispatchBlock(iq.data(), 32, 1.0e6);

    // dispatchBlock is a barrier: every handler has finished when it returns.
    for (auto& h : hs) {
        CHECK(h->done);
        CHECK(h->metAll);   // all handlers were inside processBlock at the same time
    }
}

TEST_CASE("Pipeline with pool delivers every block exactly once", "[pipeline][threads]") {
    ScopedTimingFlag timing(false);
    QThreadPool pool;
    pool.setMaxThreadCount(4);
    Pipeline p(&pool);

    std::vector<std::unique_ptr<RecordingHandler>> hs;
    for (int i = 0; i < 4; ++i) {
        hs.push_back(std::make_unique<RecordingHandler>());
        p.addHandler(hs.back().get());
    }

    BlockMeta meta;
    std::vector<float> iq(2 * 32, 0.0f);
    for (int i = 0; i < 200; ++i) {
        if (i % 2) p.dispatchBlock(iq.data(), 32, 1.0e6, meta);
        else       p.dispatchBlock(iq.data(), 32, 1.0e6);
    }

    for (auto& h : hs) {
        CHECK(h->blocks == 200);
        CHECK(h->metaBlocks == 100);
    }
}

TEST_CASE("Pipeline with pool but a single handler stays on the caller thread", "[pipeline][threads]") {
    ScopedTimingFlag timing(false);
    QThreadPool pool;
    Pipeline p(&pool);
    RecordingHandler h;
    p.addHandler(&h);

    std::vector<float> iq(2 * 8, 0.0f);
    p.dispatchBlock(iq.data(), 8, 1.0e6);
    CHECK(h.blocks == 1);
    CHECK(h.threadId == std::this_thread::get_id());
}

// ─────────────────────────────────────────────────────────────────────────────
// ProfilingHandler wrapping (pipeline_timing log category)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline wraps handlers in ProfilingHandler when timing is enabled", "[pipeline][profiling]") {
    ScopedTimingFlag timing(true);
    Pipeline p;
    RecordingHandler a, b;
    p.addHandler(&a);
    p.addHandler(&b);

    // Notifications pass through the wrapper transparently.
    // (No blocks are dispatched here: that would make the wrapper's destructor
    // append a timing summary to the real application log.)
    p.notifyStarted(5.0e6);
    p.notifyRetune(145.0e6);
    CHECK(a.started == 1);
    CHECK(a.startedSr == 5.0e6);
    CHECK(a.retuneHz == 145.0e6);

    // Removal by the original pointer must find and drop the wrapper.
    p.removeHandler(&a);
    p.notifyStopped();
    CHECK(a.stopped == 0);
    CHECK(b.stopped == 1);
}

TEST_CASE("Timing flag only affects handlers added after it changes", "[pipeline][profiling]") {
    Pipeline p;
    RecordingHandler plain, wrapped;
    {
        ScopedTimingFlag off(false);
        p.addHandler(&plain);
    }
    {
        ScopedTimingFlag on(true);
        p.addHandler(&wrapped);
    }

    p.notifyStarted(1.0e6);
    CHECK(plain.started == 1);
    CHECK(wrapped.started == 1);

    p.removeHandler(&wrapped);
    p.removeHandler(&plain);
    p.notifyStopped();
    CHECK(plain.stopped == 0);
    CHECK(wrapped.stopped == 0);
}
