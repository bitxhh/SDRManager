cmake_minimum_required(VERSION 3.16)
project(SDRManager)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ─── Qt ───────────────────────────────────────────────────────────────────────
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.10.0/mingw_64")
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport Multimedia Network)

# ─── LimeSuite ────────────────────────────────────────────────────────────────
set(LIMESUITE_ROOT "C:/LimeSuite")
include_directories(${LIMESUITE_ROOT}/include)
link_directories(${LIMESUITE_ROOT}/lib)

# ─── FFTW (single precision) ──────────────────────────────────────────────────
set(FFTW_ROOT        "${CMAKE_SOURCE_DIR}/external/FFTW")
set(FFTW_INCLUDE_DIR "${FFTW_ROOT}/include")
set(FFTW_LIB_DIR     "${FFTW_ROOT}/lib")
set(FFTW_BIN_DIR     "${FFTW_ROOT}/bin")



# ─── QCustomPlot ──────────────────────────────────────────────────────────────
set(QCUSTOMPLOT_DIR "${CMAKE_SOURCE_DIR}/external/qcustomplot")
add_library(qcustomplot STATIC
        ${QCUSTOMPLOT_DIR}/qcustomplot.cpp
        ${QCUSTOMPLOT_DIR}/qcustomplot.h
)
target_include_directories(qcustomplot PUBLIC "${QCUSTOMPLOT_DIR}")
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)

if (MINGW)
    target_compile_options(qcustomplot PRIVATE -Wa,-mbig-obj -g0)
endif ()

set(AVX2_FLAGS -mavx2 -mfma)

# ─── Main executable ──────────────────────────────────────────────────────────
# Directory layout:
#   Application/  — UI layer (Qt widgets only, no direct hardware access)
#   Hardware/     — Device backends (LimeSDR, SoapySDR, I/Q file), RxWorker, TxWorker, DeviceController
#   DSP/          — Signal processing: FFT/waterfall, modems (FM/NFM/AM/SAM/SSB/CW), IqCombiner, recorders
#   Audio/        — Audio output: FmAudioOutput (QAudioSink wrapper + resampler)
#   Core/         — Shared interfaces and utilities: IDevice, Pipeline, Logger, DeviceSettings
add_executable(SDRManager
        main.cpp
        resources.qrc

        Application/Application.cpp
        Application/Application.h
        Application/RxController.cpp
        Application/RxController.h
        Application/TxController.cpp
        Application/TxController.h
        Application/SessionManager.cpp
        Application/SessionManager.h
        Application/ClassifierController.cpp
        Application/ClassifierController.h
        Application/ChannelPanel.cpp
        Application/ChannelPanel.h
        Application/DemodulatorPanel.cpp
        Application/DemodulatorPanel.h
        Application/RadioMonitorPage.cpp
        Application/RadioMonitorPage.h
        Application/WaterfallView.cpp
        Application/WaterfallView.h

        Application/CombinedRxController.cpp
        Application/CombinedRxController.h
        Application/RecordingSettingsDialog.cpp
        Application/RecordingSettingsDialog.h
        Application/LoggerOptionsDialog.cpp
        Application/LoggerOptionsDialog.h

        DSP/ClassifierHandler.cpp
        DSP/ClassifierHandler.h

        Hardware/LimeDevice.cpp
        Hardware/LimeDevice.h
        Hardware/LimeDeviceManager.cpp
        Hardware/LimeDeviceManager.h
        Hardware/RxWorker.cpp
        Hardware/RxWorker.h
        Hardware/TxWorker.cpp
        Hardware/TxWorker.h
        Hardware/DeviceController.cpp
        Hardware/DeviceController.h
        Hardware/LimeSyncController.cpp
        Hardware/LimeSyncController.h
        Hardware/FileDevice.cpp
        Hardware/FileDevice.h
        Hardware/SoapyApi.cpp
        Hardware/SoapyApi.h
        Hardware/SoapyDevice.cpp
        Hardware/SoapyDevice.h
        Hardware/SoapyDeviceManager.cpp
        Hardware/SoapyDeviceManager.h
        Hardware/CompositeDeviceManager.cpp
        Hardware/CompositeDeviceManager.h

        DSP/FftProcessor.cpp
        DSP/FftProcessor.h
        DSP/FftHandler.cpp
        DSP/FftHandler.h
        DSP/FftPlannerLock.h
        DSP/WaterfallHandler.cpp
        DSP/WaterfallHandler.h
        DSP/BandpassExporter.cpp
        DSP/BandpassExporter.h
        DSP/BandpassHandler.cpp
        DSP/BandpassHandler.h
        DSP/DspUtils.cpp
        DSP/DspUtils.h
        DSP/IModem.h
        DSP/IModulator.h
        DSP/ModemTypes.h
        DSP/ChannelModem.cpp
        DSP/ChannelModem.h
        DSP/ModemHandler.cpp
        DSP/ModemHandler.h
        DSP/FmModem.cpp
        DSP/FmModem.h
        DSP/FmModemHandler.cpp
        DSP/FmModemHandler.h
        DSP/AmModem.cpp
        DSP/AmModem.h
        DSP/AmModemHandler.cpp
        DSP/AmModemHandler.h
        DSP/NfmModem.cpp
        DSP/NfmModem.h
        DSP/NfmModemHandler.cpp
        DSP/NfmModemHandler.h
        DSP/SsbModem.cpp
        DSP/SsbModem.h
        DSP/SsbModemHandler.cpp
        DSP/SsbModemHandler.h
        DSP/CwModem.cpp
        DSP/CwModem.h
        DSP/CwModemHandler.cpp
        DSP/CwModemHandler.h
        DSP/SamModem.cpp
        DSP/SamModem.h
        DSP/SamModemHandler.cpp
        DSP/SamModemHandler.h
        DSP/ModemRegistry.cpp
        DSP/ModemRegistry.h
        DSP/RawFileHandler.cpp
        DSP/RawFileHandler.h
        DSP/AudioFileHandler.cpp
        DSP/AudioFileHandler.h
        DSP/IqCombiner.cpp
        DSP/IqCombiner.h
        DSP/ToneGenerator.cpp
        DSP/ToneGenerator.h

        Audio/FmAudioOutput.cpp
        Audio/FmAudioOutput.h

        Core/DeviceSettings.cpp
        Core/DeviceSettings.h
        Core/FileNaming.cpp
        Core/FileNaming.h
        Core/IDevice.h
        Core/IDeviceManager.h
        Core/IPipelineHandler.h
        Core/ILogger.h
        Core/Pipeline.cpp
        Core/Pipeline.h
        Core/LimeException.h
        Core/Logger.cpp
        Core/Logger.h
        Core/LoggerConfig.cpp
        Core/LoggerConfig.h
        Core/RecordingSettings.h
        Core/WaterfallSettings.h
)

target_include_directories(SDRManager PRIVATE
        "${FFTW_INCLUDE_DIR}"
        "${CMAKE_SOURCE_DIR}/Core"
        "${CMAKE_SOURCE_DIR}/Hardware"
        "${CMAKE_SOURCE_DIR}/DSP"
        "${CMAKE_SOURCE_DIR}/Audio"
)

target_compile_options(SDRManager PRIVATE ${AVX2_FLAGS})

target_link_libraries(SDRManager
        PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::PrintSupport
        Qt6::Multimedia
        Qt6::Network
        qcustomplot
        LimeSuite.lib
        "${FFTW_LIB_DIR}/libfftw3f-3.dll.a"
        ${APP_RESOURCES}
)

# ─── Post-build ───────────────────────────────────────────────────────────────
add_custom_command(TARGET SDRManager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIMESUITE_ROOT}/bin/LimeSuite.dll"
        "$<TARGET_FILE_DIR:SDRManager>"
        COMMENT "Copying LimeSuite.dll"
)

add_custom_command(TARGET SDRManager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFTW_BIN_DIR}/libfftw3f-3.dll"
        "$<TARGET_FILE_DIR:SDRManager>/libfftw3f-3.dll"
        COMMENT "Copying FFTW float DLL"
)

add_custom_command(TARGET SDRManager POST_BUILD
        COMMAND "C:/Qt/6.10.0/mingw_64/bin/windeployqt.exe"
        --no-translations
        --no-opengl-sw
        "$<TARGET_FILE:SDRManager>"
        COMMENT "Running windeployqt"
)

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

include(FetchContent)
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(SDRManagerTests
        Tests/test_fmdemod.cpp
        Tests/test_amdemod.cpp
        Tests/test_nfmdemod.cpp
        Tests/test_ssbdemod.cpp
        Tests/test_cwdemod.cpp
        Tests/test_samdemod.cpp
        Tests/test_dsputils.cpp
        Tests/test_fftprocessor.cpp
        Tests/test_iqcombiner.cpp

        DSP/DspUtils.cpp
        DSP/ChannelModem.cpp
        DSP/FmModem.cpp
        DSP/AmModem.cpp
        DSP/NfmModem.cpp
        DSP/SsbModem.cpp
        DSP/CwModem.cpp
        DSP/SamModem.cpp
        DSP/FftProcessor.cpp
        DSP/IqCombiner.cpp
        Core/Pipeline.cpp
        Core/Logger.cpp
        Core/LoggerConfig.cpp
)

target_include_directories(SDRManagerTests PRIVATE
        "${CMAKE_SOURCE_DIR}/Core"
        "${CMAKE_SOURCE_DIR}/DSP"
        "${FFTW_INCLUDE_DIR}"
)

target_compile_options(SDRManagerTests PRIVATE ${AVX2_FLAGS})

target_link_libraries(SDRManagerTests
        PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        Qt6::Concurrent
        "${FFTW_LIB_DIR}/libfftw3f-3.dll.a"
)

# Copy FFTW DLL next to the test binary so it can run on Windows
add_custom_command(TARGET SDRManagerTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFTW_BIN_DIR}/libfftw3f-3.dll"
        "$<TARGET_FILE_DIR:SDRManagerTests>/libfftw3f-3.dll"
        COMMENT "Copying FFTW float DLL for tests"
)

include(Catch)
catch_discover_tests(SDRManagerTests)
