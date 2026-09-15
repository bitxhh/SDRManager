# SDRManager

[English](README.md) | **Русский**

SDR-приёмник на Qt6/C++ для LimeSDR и устройств SoapySDR: спектр и водопад в реальном времени,
демодуляция FM/NFM/AM/SAM/SSB/CW, когерентное сложение двух каналов, запись и воспроизведение I/Q.

<!-- Скриншоты: скоро -->

## Возможности

- **Несколько источников сигнала**
  - LimeSDR через LimeSuite (оба RX-канала + TX)
  - любые устройства SoapySDR (RTL-SDR, HackRF, Airspy, …), библиотека подгружается во время работы
  - воспроизведение записанных I/Q-файлов (`.cf32` / `.cs16`)
- **Спектр и водопад в реальном времени**: EMA-сглаживание, масштабирование, тёмная тема графиков
- **До 4 независимых демодуляторов** на одном потоке, у каждого свои VFO, полоса, громкость и запись
- **Режимы**: WBFM, NFM, AM, SAM (синхронный AM), USB, LSB, CW. Режим можно переключать на лету.
- **Когерентное сложение** каналов RX0 + RX1 LimeSDR с автокалибровкой фазы
- **Запись**
  - сырой I/Q (`.cf32`) по каналам или суммарный
  - отфильтрованный по полосе I/Q для каждого демодулятора
  - демодулированный звук (`.wav`)
- **Настройки сохраняются для каждого устройства**: частота дискретизации, усиления, частота и панели демодуляторов восстанавливаются при следующем запуске
- **Тестовый тон на передачу**: генератор синусоиды на TX0
- **ИИ-классификатор модуляции (опционально)**: Python-сервис, подключённый через локальный TCP-сокет (`Python/classifier_service.py`)

## Архитектура

```
DeviceManager (LimeSDR / SoapySDR / I/Q-файл)
      │
      ▼
RxWorker (QThread на каждый RX-канал) ──► PrePipeline ──► IqCombiner
                                                              │
                                                              ▼
                                               Combined Pipeline (QThreadPool)
                     ┌──────────────┬──────────────────┬──────┴─────────┬──────────────────┐
                     ▼              ▼                  ▼                ▼                  ▼
                FftHandler   WaterfallHandler   ModemHandler ×N    RawFileHandler   Bandpass / Audio
                  спектр          водопад         демодулятор         → .cf32        file handlers
                                                       │
                                                       ▼
                                                 FmAudioOutput
                                          ресемплинг + АРУ → WASAPI
```

UI-поток только отрисовывает и отправляет команды. Приём I/Q идёт в отдельных `QThread`,
а обработчики сигнала работают параллельно в общем пуле потоков.
Подробнее в [docs/architecture.md](docs/architecture.md).

## Сборка

### Требования

- **Windows** + **MinGW** (GCC 13+, из комплекта Qt)
- **CMake** 3.16+
- **Qt 6.10**: Widgets, Concurrent, PrintSupport, Multimedia, Network
- **LimeSuite**: заголовки + `LimeSuite.dll` в `C:/LimeSuite`
- **SoapySDR** *(опционально, во время работы)*: `SoapySDR.dll` в `PATH` или в `C:/Program Files/PothosSDR/bin`.
  Без неё устройства SoapySDR просто не показываются.
- **Процессор с AVX2 + FMA**: сборка использует `-mavx2 -mfma`
- В комплекте, в `external/`: **FFTW3** (одинарная точность) и **QCustomPlot**

Пути к Qt и LimeSuite задаются в начале `CMakeLists.txt`. Поправьте их, если у вас они другие.

### Команды сборки

```bash
# Release (рекомендуется)
cmake -S . -B cmake-build-release-mingw-qt -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release-mingw-qt --target SDRManager
```

Для прослушивания используйте **Release**. В Debug стоит укороченный FIR, и сборка не тянет ≥15 MS/s.
После сборки запускается `windeployqt`, а рядом с исполняемым файлом копируются `LimeSuite.dll` и `libfftw3f-3.dll`.

### Тесты

```bash
cmake --build cmake-build-release-mingw-qt --target SDRManagerTests
ctest --test-dir cmake-build-release-mingw-qt
```

Юнит-тесты на Catch2 покрывают FFT, IqCombiner, DSP-утилиты и все демодуляторы (FM, NFM, AM, SAM, SSB, CW).

### Запуск

1. Запустите `SDRManager.exe`.
2. Выберите устройство или нажмите **Open I/Q file...**, чтобы воспроизвести запись.
3. Инициализируйте устройство, задайте частоту дискретизации, откалибруйте и запустите поток.

Настройки и лог (`sdrmanager.log`) хранятся в `%APPDATA%\SDRManager`.

## Настройка железа (LimeSDR)

LimeSDR настраивается так же, как в **ExtIO_LimeSDR** (плагин для HDSDR), это проверенный эталон:

- Полоса аналогового ФНЧ = частота дискретизации
- TIA защищается при вызовах `LMS_SetLPFBW` (обход бага LimeSuite)
- Регистр компенсации PGA (`RCC_CTL_PGA_RBB`) обновляется при каждом изменении усиления
- Полоса калибровки = max(частота дискретизации, 2.5 МГц)

Подробнее в [docs/hardware.md](docs/hardware.md).

## Структура проекта

```
Core/           Интерфейсы и инфраструктура (IDevice, IPipelineHandler, Pipeline, Logger, настройки)
Hardware/       Бэкенды устройств (LimeSDR, SoapySDR, I/Q-файл), RX/TX-воркеры
DSP/            FFT, водопад, модемы, IqCombiner, запись
Audio/          Аудиовывод (ресемплер, АРУ, QAudioSink)
Application/    Qt UI (выбор устройства, радиомониторинг, панели демодуляторов, TX)
Tests/          Юнит-тесты (Catch2)
Python/         Опциональный сервис классификации модуляции
external/       Зависимости в комплекте (FFTW, QCustomPlot)
docs/           Архитектура, DSP-цепочки, заметки по железу
```

## Документация

Документация написана на английском:

- [docs/architecture.md](docs/architecture.md): компоненты, модель потоков, интерфейсы, поток данных
- [docs/dsp.md](docs/dsp.md): DSP-цепочки и параметры всех модемов
- [docs/hardware.md](docs/hardware.md): инициализация LimeSDR, особенности железа, структура усиления

## Лицензия

Исходный код проекта распространяется по [лицензии MIT](LICENSE).

У части сторонних зависимостей копилефт-лицензии:
- QCustomPlot: GPL-3.0
- FFTW: GPL-2.0+
- Qt: LGPL-3.0 / GPL

Распространяемый бинарник, который их линкует, должен соблюдать их условия. Остальные зависимости разрешительные:
LimeSuite под Apache-2.0, SoapySDR под Boost.
