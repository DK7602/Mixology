# CPU check: a big session (48 real Mixology Links + Mixology) timed the way Cubase runs it.
# Build: -DMIX_BUILD_LINKCHECK=ON. Run: MixologyCpuCheck "<Mixology Link.vst3>" "<Mixology.vst3>" [tracks] [block] [--strict]

juce_add_console_app(MixologyCpuCheck PRODUCT_NAME "MixologyCpuCheck")
target_sources(MixologyCpuCheck PRIVATE cpucheck.cpp)
target_compile_definitions(MixologyCpuCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(MixologyCpuCheck PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
