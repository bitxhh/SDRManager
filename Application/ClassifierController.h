#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>

#include <functional>
#include <map>

class RxController;
class ClassifierHandler;
class IPipelineHandler;

// ---------------------------------------------------------------------------
// ClassifierController — owns the Python subprocess and the TCP connection
// to the classifier service.
//
// Lifecycle:
//   start() → QProcess launches Python script → connectToService() tries to
//   connect (retries until success or process exits) → socket connected →
//   one ClassifierHandler per slot added to the RX pipeline.
//
//   stop() / process crash → handlers removed from pipeline →
//   classifierStopped() emitted → UI shows "Unavailable".
//
// Slots: every demodulator gets its own slot (and ClassifierHandler), all
// sharing one socket. The slot number travels in the frame (protocol v3) and
// comes back in the JSON reply, so classificationReady() says which
// demodulator the result is for.
//
// The pipeline is reached through attach/detach callbacks, so the same
// controller works with RxController and CombinedRxController. Controllers
// drop their extra handlers on stream teardown; call reattach() after every
// stream (re)start.
//
// All members live on the main thread.
// ClassifierHandler::frameReady is connected via QueuedConnection so the
// worker-thread signal safely reaches this object's sendFrame() slot.
// ---------------------------------------------------------------------------
class ClassifierController : public QObject {
    Q_OBJECT

public:
    using HandlerFn = std::function<void(IPipelineHandler*)>;

    ClassifierController(HandlerFn attach, HandlerFn detach, QObject* parent = nullptr);
    // Single-channel convenience: RxController pipeline, slot 0.
    explicit ClassifierController(RxController* appCtrl, QObject* parent = nullptr);
    ~ClassifierController() override;

    void start(const QString& pythonExe, const QString& scriptPath);
    void stop();

    [[nodiscard]] bool isRunning() const;

    // Slots can be added/removed at any time; while connected the handler is
    // created/attached (or detached/deleted) immediately.
    void addSlot(int slot);
    void removeSlot(int slot);

    // Channel the slot's handler extracts before sending (see
    // ClassifierHandler). Remembered across start()/stop(). bandwidthHz <= 0
    // → wideband.
    void setChannel(int slot, double offsetHz, double bandwidthHz);
    void setChannel(double offsetHz, double bandwidthHz) { setChannel(0, offsetHz, bandwidthHz); }

    // Adaptive frame interval: while a slot keeps returning the same class
    // the handler's send interval steps up (kIntervalFastMs → Mid → Slow);
    // a class change or a new channel drops it back to kIntervalFastMs.
    static constexpr int kIntervalFastMs = 100;
    static constexpr int kIntervalMidMs  = 1000;
    static constexpr int kIntervalSlowMs = 5000;
    static constexpr int kStableForMid   = 3;    // identical results in a row
    static constexpr int kStableForSlow  = 10;
    // Pure step function, public for tests: interval for a streak length.
    [[nodiscard]] static int intervalForStreak(int stable);

    // Re-adds the handlers after the stream was restarted (no-op if not connected).
    void reattach();

    static constexpr int kPort = 52001;

signals:
    void classificationReady(int slot, const QString& type, double confidence);
    void classifierStarted();
    void classifierStopped();
    void classifierError(const QString& msg);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void connectToService();
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void sendFrame(const QByteArray& data);

private:
    struct Slot {
        double             offsetHz{0.0};
        double             bandwidthHz{0.0};
        ClassifierHandler* handler{nullptr};
        QString            lastType;        // adaptive interval state
        int                stable{0};       // identical results in a row
    };

    // Updates the slot's streak from a new result and retunes its handler.
    void adaptInterval(int slot, const QString& type);
    void resetInterval(Slot& s);

    void createHandler(int slot, Slot& s);
    void attachHandlers();
    void detachHandlers();
    void deleteHandlers(bool later);
    void teardown();

    HandlerFn          attach_;
    HandlerFn          detach_;
    std::map<int, Slot> slots_;
    bool               connected_{false};   // socket up, handlers in the pipeline
    QProcess*          process_{nullptr};
    QTcpSocket*        socket_{nullptr};
    QTimer*            connectTimer_{nullptr};   // retry until Python is ready
    QByteArray         readBuf_;
};
