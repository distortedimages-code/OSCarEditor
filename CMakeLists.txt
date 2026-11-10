cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(OSCarEditor VERSION 0.4.0 LANGUAGES C CXX)

# GitHub repository whose releases the plugin checks for updates (public releases).
set(OSCAR_UPDATE_REPO "distortedimages-code/OSCarEditor" CACHE STRING "owner/repo checked for updates (empty = no update check)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout with -DJUCE_DIR=/path/to/JUCE, otherwise download JUCE 8.0.4.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#==============================================================================
# SysEx codec, no JUCE inside: shared by the plugin and the tests.
add_library(oscar_core STATIC
    Source/core/OscarSysex.cpp
    Source/core/OscarParams.cpp
    Source/core/Generator.cpp)
target_compile_features(oscar_core PUBLIC cxx_std_17)
set_target_properties(oscar_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

#==============================================================================
juce_add_plugin(OSCarEditor
    COMPANY_NAME "OSCarTools"
    PRODUCT_NAME "OSCar Editor"
    BUNDLE_ID com.oscartools.oscareditor
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Otls
    PLUGIN_CODE Oscr
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Tools
    FORMATS AU VST3 Standalone)

juce_generate_juce_header(OSCarEditor)

# Fonts (SIL Open Font License, see Resources/Fonts) and the French translation
juce_add_binary_data(OscarAssets SOURCES
    Resources/Fonts/BarlowSemiCondensed-SemiBold.ttf
    Resources/Fonts/BarlowSemiCondensed-Bold.ttf
    Resources/Fonts/CormorantSC-SemiBold.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf
    Resources/Lang/fr.txt)
set_target_properties(OscarAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(OSCAR_UI_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BankDocument.cpp
    Source/MidiLink.cpp
    Source/Updater.cpp
    Source/ui/Atelier.cpp
    Source/ui/Lang.cpp
    Source/ui/Panels.cpp)

set(OSCAR_DEFINITIONS
    OSCAR_VERSION="${PROJECT_VERSION}"
    OSCAR_UPDATE_REPO="${OSCAR_UPDATE_REPO}")

target_sources(OSCarEditor PRIVATE ${OSCAR_UI_SOURCES})

target_compile_definitions(OSCarEditor PUBLIC
    ${OSCAR_DEFINITIONS}
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(OSCarEditor
    PRIVATE
        oscar_core
        OscarAssets
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
option(OSCAR_BUILD_TESTS "Build the unit tests" ON)

if(OSCAR_BUILD_TESTS)
    enable_testing()

    add_executable(CoreTests Tests/CoreTests.cpp)
    target_link_libraries(CoreTests PRIVATE oscar_core)
    add_test(NAME CoreTests
             COMMAND CoreTests ${CMAKE_CURRENT_SOURCE_DIR}/Tests/factory_codemechanic.syx)

    add_executable(GeneratorTests Tests/GeneratorTests.cpp)
    target_link_libraries(GeneratorTests PRIVATE oscar_core)
    add_test(NAME GeneratorTests COMMAND GeneratorTests)

    juce_add_console_app(DocTests PRODUCT_NAME "DocTests")
    juce_generate_juce_header(DocTests)
    target_sources(DocTests PRIVATE Tests/DocTests.cpp Source/BankDocument.cpp)
    target_compile_definitions(DocTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(DocTests PRIVATE
        oscar_core
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME DocTests COMMAND DocTests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # End-to-end MIDI behaviour through a virtual OSCar port
    juce_add_console_app(ProcessorTests PRODUCT_NAME "ProcessorTests")
    juce_generate_juce_header(ProcessorTests)
    target_sources(ProcessorTests PRIVATE Tests/ProcessorTests.cpp ${OSCAR_UI_SOURCES})
    target_compile_definitions(ProcessorTests PRIVATE
        ${OSCAR_DEFINITIONS}
        JucePlugin_Name="OSCar Editor"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ProcessorTests PRIVATE
        oscar_core
        OscarAssets
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME ProcessorTests COMMAND ProcessorTests)
    set_tests_properties(ProcessorTests PROPERTIES
        ENVIRONMENT "OSCAR_EDITOR_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-home")

    # Update checker: version compare, GitHub answer parsing, checksum, bundle replacement
    juce_add_console_app(UpdaterTests PRODUCT_NAME "UpdaterTests")
    juce_generate_juce_header(UpdaterTests)
    target_sources(UpdaterTests PRIVATE Tests/UpdaterTests.cpp Source/Updater.cpp)
    target_compile_definitions(UpdaterTests PRIVATE ${OSCAR_DEFINITIONS} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(UpdaterTests PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME UpdaterTests COMMAND UpdaterTests)

    # Every text of the interface has a French translation with the same placeholders
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME TranslationTests
                 COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/Tests/check_translations.py ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()
