#include "ClassifierController.h"
#include "../DSP/ClassifierHandler.h"
#include "RxController.h"
#include "Logger.h"

#include <QJsonDocument>
#include <QJsonObject>

ClassifierController::ClassifierController(HandlerFn attach, HandlerFn detach, QObject* parent)
    : QObject(parent)
    , attach_(std::move(attach))
    , detach_(std::move(detach))
{}

ClassifierController::ClassifierController(RxController* appCtrl, QObject* parent)
    : ClassifierController(
          [appCtrl](IPipelineHandler* h) { if (appCtrl) appCtrl->addExtraHandler(h); },
          [appCtrl](IPipelineHandler* h) { if (appCtrl) appCtrl->removeExtraHandler(h); },
          parent)
{
    addSlot(0);
}

ClassifierController::~ClassifierController() {
    teardown();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void ClassifierController::start(const QString& pythonExe, const QString& scriptPath) {
    if (isRunning()) return;

    LOG_INFO("ClassifierController: starting " + scriptPath.toStdString());

    for (auto& [slot, s] : slots_) createHandler(slot, s);

    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected,    this, &ClassifierController::onSocketConnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &ClassifierController::onSocketReadyRead);
    connect(socket_, &QTcpSocket::errorOccurred,this, &ClassifierController::onSocketError);

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::started,         this, &ClassifierController::onProcessStarted);
    connect(process_, &QProcess::finished,
            this, &ClassifierController::onProcessFinished);
    connect(process_, &QProcess::readyRead, this, [this]() {
        LOG_INFO("[classifier] " + process_->readAll().trimmed().toStdString());
    });

    process_->start(pythonExe, {scriptPath});
}

void ClassifierController::stop() {
    teardown();
    emit classifierStopped();
}

void ClassifierController::addSlot(int slot) {
    if (slots_.count(slot)) return;
    Slot& s = slots_[slot];
    if (process_) createHandler(slot, s);
    if (connected_ && s.handler && attach_) attach_(s.handler);
}

void ClassifierController::removeSlot(int slot) {
    auto it = slots_.find(slot);
    if (it == slots_.end()) return;
    if (ClassifierHandler* h = it->second.handler) {
        if (connected_ && detach_) detach_(h);
        delete h;
    }
    slots_.erase(it);
}

void ClassifierController::setChannel(int slot, double offsetHz, double bandwidthHz) {
    auto it = slots_.find(slot);
    if (it == slots_.end()) return;
    Slot& s = it->second;
    const bool changed = s.offsetHz != offsetHz || s.bandwidthHz != bandwidthHz;
    s.offsetHz    = offsetHz;
    s.bandwidthHz = bandwidthHz;
    if (s.handler) s.handler->setChannel(offsetHz, bandwidthHz);
    if (changed) resetInterval(s);   // new station — classify it quickly
}

int ClassifierController::intervalForStreak(int stable) {
    if (stable >= kStableForSlow) return kIntervalSlowMs;
    if (stable >= kStableForMid)  return kIntervalMidMs;
    return kIntervalFastMs;
}

void ClassifierController::resetInterval(Slot& s) {
    s.lastType.clear();
    s.stable = 0;
    if (s.handler) s.handler->setIntervalMs(kIntervalFastMs);
}

void ClassifierController::adaptInterval(int slot, const QString& type) {
    auto it = slots_.find(slot);
    if (it == slots_.end()) return;
    Slot& s = it->second;
    if (type == s.lastType) {
        ++s.stable;
    } else {
        s.lastType = type;
        s.stable   = 0;
    }
    if (s.handler) s.handler->setIntervalMs(intervalForStreak(s.stable));
}

void ClassifierController::reattach() {
    if (connected_) attachHandlers();
}

bool ClassifierController::isRunning() const {
    return process_ && process_->state() != QProcess::NotRunning;
}

// ---------------------------------------------------------------------------
// Process slots
// ---------------------------------------------------------------------------
void ClassifierController::onProcessStarted() {
    LOG_INFO("ClassifierController: process started, connecting to service…");
    connectTimer_ = new QTimer(this);
    connectTimer_->setInterval(500);
    connect(connectTimer_, &QTimer::timeout, this, &ClassifierController::connectToService);
    connectTimer_->start();
    connectToService();  // try immediately
}

void ClassifierController::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    const QString reason = (status == QProcess::CrashExit)
                           ? "crashed"
                           : QString("exited with code %1").arg(exitCode);
    LOG_WARN("ClassifierController: process " + reason.toStdString());

    detachHandlers();

    if (connectTimer_) { connectTimer_->stop(); connectTimer_->deleteLater(); connectTimer_ = nullptr; }
    if (socket_)       { socket_->abort(); socket_->deleteLater(); socket_ = nullptr; }
    if (process_)      { process_->deleteLater(); process_ = nullptr; }
    deleteHandlers(/*later=*/true);

    emit classifierError("Classifier service " + reason + ".");
    emit classifierStopped();
}

// ---------------------------------------------------------------------------
// Socket connection
// ---------------------------------------------------------------------------
void ClassifierController::connectToService() {
    if (!socket_) return;
    if (socket_->state() != QAbstractSocket::UnconnectedState) return;
    socket_->connectToHost("127.0.0.1", kPort);
}

void ClassifierController::onSocketConnected() {
    LOG_INFO("ClassifierController: connected to classifier service");
    if (connectTimer_) { connectTimer_->stop(); connectTimer_->deleteLater(); connectTimer_ = nullptr; }
    connected_ = true;
    attachHandlers();
    emit classifierStarted();
}

void ClassifierController::onSocketError(QAbstractSocket::SocketError /*err*/) {
    // Connection refused = Python not ready yet; retry via connectTimer_.
    // Only log once connected (after classifierStarted was emitted).
    if (!connectTimer_) {
        LOG_WARN("ClassifierController: socket error — "
                 + socket_->errorString().toStdString());
        emit classifierError("Lost connection to classifier service.");
        detachHandlers();
    }
    // While connectTimer_ is running: silently retry.
}

// ---------------------------------------------------------------------------
// Data flow: C++ → Python
// ---------------------------------------------------------------------------
void ClassifierController::sendFrame(const QByteArray& data) {
    if (socket_ && socket_->state() == QAbstractSocket::ConnectedState)
        socket_->write(data);
}

// ---------------------------------------------------------------------------
// Data flow: Python → C++
// ---------------------------------------------------------------------------
void ClassifierController::onSocketReadyRead() {
    readBuf_ += socket_->readAll();

    // Responses are newline-terminated JSON objects.
    while (true) {
        const int nl = readBuf_.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = readBuf_.left(nl).trimmed();
        readBuf_.remove(0, nl + 1);

        if (line.isEmpty()) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            LOG_WARN("ClassifierController: invalid JSON: " + line.toStdString());
            continue;
        }
        const QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            LOG_WARN("ClassifierController: service error: "
                     + obj.value("error").toString().toStdString());
            continue;
        }
        const int     slot       = obj.value("slot").toInt(0);   // absent before v3
        const QString type       = obj.value("type").toString("Unknown");
        const double  confidence = obj.value("confidence").toDouble(0.0);
        adaptInterval(slot, type);
        emit classificationReady(slot, type, confidence);
    }
}

// ---------------------------------------------------------------------------
// Handler pipeline wiring
// ---------------------------------------------------------------------------
void ClassifierController::createHandler(int slot, Slot& s) {
    if (s.handler) return;
    s.handler = new ClassifierHandler(this, slot);
    s.handler->setChannel(s.offsetHz, s.bandwidthHz);
    s.lastType.clear();
    s.stable = 0;   // fresh handler starts at kIntervalFastMs
    // Connect handler signal → socket write (cross-thread: worker → main).
    connect(s.handler, &ClassifierHandler::frameReady,
            this,      &ClassifierController::sendFrame, Qt::QueuedConnection);
}

void ClassifierController::attachHandlers() {
    if (!attach_) return;
    for (auto& [slot, s] : slots_)
        if (s.handler) attach_(s.handler);
}

void ClassifierController::detachHandlers() {
    connected_ = false;
    if (!detach_) return;
    for (auto& [slot, s] : slots_)
        if (s.handler) detach_(s.handler);
}

void ClassifierController::deleteHandlers(bool later) {
    for (auto& [slot, s] : slots_) {
        if (!s.handler) continue;
        if (later) s.handler->deleteLater();
        else       delete s.handler;
        s.handler = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------
void ClassifierController::teardown() {
    detachHandlers();

    if (connectTimer_) { connectTimer_->stop(); delete connectTimer_; connectTimer_ = nullptr; }

    if (socket_) {
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState)
            socket_->waitForDisconnected(500);
        delete socket_;
        socket_ = nullptr;
    }

    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->terminate();
        if (!process_->waitForFinished(2000))
            process_->kill();
    }
    delete process_; process_ = nullptr;
    deleteHandlers(/*later=*/false);
}
