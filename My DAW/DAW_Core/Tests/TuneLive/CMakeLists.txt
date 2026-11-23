cmake_minimum_required(VERSION 3.22)
project(ApexTuneLiveNativeSmoke VERSION 0.1.0 LANGUAGES CXX)
if (NOT JUCE_SOURCE_DIR OR NOT EXISTS "${JUCE_SOURCE_DIR}/modules/juce_audio_processors/juce_audio_processors.h")
    message(FATAL_ERROR "Set JUCE_SOURCE_DIR to a real JUCE 8.0.12 checkout")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_juce" EXCLUDE_FROM_ALL)

juce_add_console_app(TuneLiveNativeRuntimeSmoke
    PRODUCT_NAME "APEX Tune Live Native Runtime Smoke")
target_sources(TuneLiveNativeRuntimeSmoke PRIVATE test_juce_native_runtime.cpp)
target_include_directories(TuneLiveNativeRuntimeSmoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../JuceLibraryCode"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Source/ThirdParty"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Source/ThirdParty/signalsmith-stretch")
target_compile_definitions(TuneLiveNativeRuntimeSmoke PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0
    JUCE_PLUGINHOST_VST3=0)
target_link_libraries(TuneLiveNativeRuntimeSmoke PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(TuneLiveNativeRuntimeSmoke PRIVATE /bigobj /utf-8)
endif()

# Separately packaged research GUI; never registers an APEX Native plugin.
juce_add_gui_app(TuneLiveResearchMonitor PRODUCT_NAME "APEX Tune Live Research Monitor")
target_sources(TuneLiveResearchMonitor PRIVATE tune_live_research_monitor.cpp)
target_include_directories(TuneLiveResearchMonitor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../JuceLibraryCode"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Source/ThirdParty"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Source/ThirdParty/signalsmith-stretch")
target_compile_definitions(TuneLiveResearchMonitor PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_ASIO=0 JUCE_PLUGINHOST_VST3=0)
target_link_libraries(TuneLiveResearchMonitor PRIVATE juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(TuneLiveResearchMonitor PRIVATE /bigobj /utf-8)
endif()
