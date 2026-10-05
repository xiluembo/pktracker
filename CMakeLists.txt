cmake_minimum_required(VERSION 3.21)

project(PokopiaTrackPlanner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PKTRACKER_MOBILE_UI "Build the touch-friendly UI (also available on desktop)" ${ANDROID})
include(CTest)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Multimedia)
if(BUILD_TESTING AND NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

qt_standard_project_setup()

# ---- midifile (leitor de Standard MIDI Files, BSD-2) ----
# SOURCE_SUBDIR aponta para um diretório inexistente de propósito: o CMakeLists
# do midifile é legado, então compilamos a biblioteca nós mesmos.
include(FetchContent)
FetchContent_Declare(midifile
    URL https://github.com/craigsapp/midifile/archive/98917df5b1bf0d6e8d4c0e5fff86d6b05343e793.zip
    URL_HASH SHA256=005cab033b5d224c26e9e093af382371acf6c0010c5caca5ed497dd8f3a6e833
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR do-not-add
)
FetchContent_MakeAvailable(midifile)

add_library(midifile_lib STATIC
    ${midifile_SOURCE_DIR}/src/Binasc.cpp
    ${midifile_SOURCE_DIR}/src/MidiEvent.cpp
    ${midifile_SOURCE_DIR}/src/MidiEventList.cpp
    ${midifile_SOURCE_DIR}/src/MidiFile.cpp
    ${midifile_SOURCE_DIR}/src/MidiMessage.cpp
)
target_include_directories(midifile_lib PUBLIC ${midifile_SOURCE_DIR}/include)
if(MSVC)
    target_compile_definitions(midifile_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---- núcleo compartilhado (modelo, serialização, simulação) ----
qt_add_library(pktracks_core STATIC
    src/AppConstants.h
    src/GridModel.cpp
    src/GridModel.h
    src/ItemTypes.cpp
    src/ItemTypes.h
    src/LaserUtils.cpp
    src/LaserUtils.h
    src/LayoutSerializer.cpp
    src/LayoutSerializer.h
    src/LayoutSimulator.cpp
    src/LayoutSimulator.h
)
target_include_directories(pktracks_core PUBLIC src)
target_link_libraries(pktracks_core PUBLIC Qt6::Core Qt6::Gui)

# ---- pipeline MIDI + geradores de pista ----
qt_add_library(pktracks_midigen STATIC
    src/midi/MidiSong.cpp
    src/midi/MidiSong.h
    src/generator/ImportProfile.h
    src/generator/NoteMapping.cpp
    src/generator/NoteMapping.h
    src/generator/TrackGenerator.cpp
    src/generator/TrackGenerator.h
)
target_link_libraries(pktracks_midigen PUBLIC pktracks_core PRIVATE midifile_lib)

# ---- UI compartilhada (canvas, áudio) ----
qt_add_library(pktracks_ui STATIC
    src/AudioEngine.cpp
    src/AudioEngine.h
    src/GridCanvas.cpp
    src/GridCanvas.h
)
target_link_libraries(pktracks_ui PUBLIC pktracks_core Qt6::Widgets Qt6::Multimedia)

# ---- wizard de importação MIDI (UI) ----
qt_add_library(pktracks_wizard STATIC
    src/wizard/MidiImportWizard.cpp
    src/wizard/MidiImportWizard.h
    src/wizard/QuantizationPreviewWidget.cpp
    src/wizard/QuantizationPreviewWidget.h
)
target_link_libraries(pktracks_wizard PUBLIC pktracks_ui pktracks_midigen)
if(PKTRACKER_MOBILE_UI)
    target_compile_definitions(pktracks_wizard PUBLIC PKTRACKER_MOBILE_UI)
endif()

# ---- planner ----
qt_add_executable(PokopiaTrackPlanner
    WIN32
    src/MainWindow.cpp
    src/MainWindow.h
    src/main.cpp
)
target_link_libraries(PokopiaTrackPlanner PRIVATE pktracks_wizard)
if(PKTRACKER_MOBILE_UI)
    target_sources(PokopiaTrackPlanner PRIVATE src/mobile/MobileUi.cpp)
endif()
if(ANDROID)
    set_target_properties(PokopiaTrackPlanner PROPERTIES
        QT_ANDROID_PACKAGE_NAME "org.pktracker.planner"
        QT_ANDROID_APP_NAME "Pokopia Track Planner"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 35
    )
endif()

# ---- wizard standalone ----
if(NOT ANDROID)
    qt_add_executable(PokopiaMidiWizard
        WIN32
        src/wizard/WizardMain.cpp
    )
    target_link_libraries(PokopiaMidiWizard PRIVATE pktracks_wizard)
endif()

# ---- testes ----
if(BUILD_TESTING AND NOT ANDROID)
    enable_testing()
    qt_add_executable(pktracks_tests
        tests/tst_pktracks.cpp
    )
    target_link_libraries(pktracks_tests PRIVATE pktracks_midigen midifile_lib Qt6::Test)
    target_compile_definitions(pktracks_tests PRIVATE PKTRACKS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME pktracks_tests COMMAND pktracks_tests)
    if(PKTRACKER_MOBILE_UI)
        qt_add_executable(pktracker_mobile_tests
            tests/tst_mobile_ui.cpp src/MainWindow.cpp src/mobile/MobileUi.cpp)
        target_link_libraries(pktracker_mobile_tests PRIVATE pktracks_wizard Qt6::Test)
        add_test(NAME pktracker_mobile_tests COMMAND pktracker_mobile_tests)
    endif()
endif()
