# Mixology Link: the small per-track plug-in (Milestone 2: Trim, meters, identity).

set(LINK_FORMATS VST3)
if(APPLE)
    list(APPEND LINK_FORMATS AU)
endif()

juce_add_plugin(MixologyLink
    PRODUCT_NAME                "Mixology Link"
    COMPANY_NAME                "Mixology Audio"
    COMPANY_WEBSITE             "https://example.com"
    BUNDLE_ID                   com.mixologyaudio.mixologylink
    PLUGIN_MANUFACTURER_CODE    Mxlg
    PLUGIN_CODE                 Mxk1
    FORMATS                     ${LINK_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    NEEDS_WEB_BROWSER           TRUE
    NEEDS_WEBVIEW2              TRUE
    VST3_CATEGORIES             Fx Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect)

# The window shares Mixology's stylesheet, building blocks, fonts and logo (served flat by file name).
set(MIX_UI "${CMAKE_SOURCE_DIR}/plugin/ui")
file(GLOB LINK_FONTS CONFIGURE_DEPENDS "${MIX_UI}/fonts/*.ttf")
juce_add_binary_data(MixologyLinkUIData
    HEADER_NAME MixologyLinkUIData.h
    NAMESPACE   MixologyLinkUI
    SOURCES
        ui/link.html
        ui/link.css
        ui/link.js
        ui/link_mock.js
        "${MIX_UI}/styles.css"
        "${MIX_UI}/ui.js"
        "${MIX_UI}/assets/logo.png"
        "${MIX_UI}/assets/wordmark.png"
        ${LINK_FONTS}
        "${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/index.js"
        "${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/check_native_interop.js")
set_target_properties(MixologyLinkUIData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(MixologyLink PRIVATE
    LinkProcessor.cpp
    LinkEditor.cpp)

target_compile_definitions(MixologyLink PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MixologyLink
    PRIVATE
        mixology_dsp
        MixologyLinkUIData
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
