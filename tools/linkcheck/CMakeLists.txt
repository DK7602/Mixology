# Cross-library check: loads the real "Mixology Link.vst3" twice, as Cubase would, and reads the Link
# registry from THIS program (a separate module, like Mixology). Build: -DMIX_BUILD_LINKCHECK=ON.
# Run: MixologyLinkCheck "<path to Mixology Link.vst3>"

juce_add_console_app(MixologyLinkCheck PRODUCT_NAME "MixologyLinkCheck")
target_sources(MixologyLinkCheck PRIVATE linkcheck.cpp)
target_compile_definitions(MixologyLinkCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(MixologyLinkCheck PRIVATE
    mixology_dsp
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
