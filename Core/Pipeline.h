#pragma once

#include "IPipelineHandler.h"
#include "ProfilingHandler.h"
#include <QObject>
#include <QThreadPool>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

// ---------------------------------------------------------------------------
// Pipeline — маршрутизатор I/Q блоков к зарегистрированным обработчикам.
//
// Потокобезопасность:
//   add/remove/clear — можно вызывать из любого потока.
//   dispatch/notify  — вызываются из RxWorker thread.
//   Список handlers копируется под мьютексом перед вызовом,
//   так что add/remove не блокируют основной цикл.
//
// Параллельный dispatch:
//   Если передан QThreadPool* (не nullptr) и handlers > 1, каждый handler
//   запускается как отдельная задача пула. dispatchBlock возвращается только
//   после завершения всех задач (барьер) — backpressure сохраняется.
//   Handlers не должны разделять изменяемое состояние между собой.
//   При pool == nullptr или одном handler — синхронный последовательный вызов.
//
// Профилирование:
//   Если на момент addHandler() включена категория LogCat::kPipelineTiming,
//   handler оборачивается в ProfilingHandler. removeHandler() принимает
//   исходный указатель — обёртка находится и удаляется сама.
// ---------------------------------------------------------------------------
class Pipeline : public QObject {
    Q_OBJECT

public:
    // pool == nullptr → синхронный режим (backward-compat, TX, одиночные handlers)
    explicit Pipeline(QThreadPool* pool = nullptr, QObject* parent = nullptr);

    void addHandler(IPipelineHandler* handler);
    void removeHandler(IPipelineHandler* handler);
    void clearHandlers();

    // Вызывается из RxWorker thread
    void dispatchBlock(const float* iq, int count, double sampleRateHz);
    void dispatchBlock(const float* iq, int count, double sampleRateHz, const BlockMeta& meta);
    void notifyStarted(double sampleRateHz);
    void notifyStopped();
    // Fan-out for IDevice::retuned — called synchronously from the UI thread
    // while the RX worker is parked, so handler state mutation is race-free.
    void notifyRetune(double newFreqHz);

private:
    QThreadPool*                   pool_{nullptr};
    std::shared_mutex              mutex_;
    std::vector<IPipelineHandler*> handlers_;
    std::vector<std::unique_ptr<ProfilingHandler>> profilers_;  // владеют обёртками из handlers_
};
