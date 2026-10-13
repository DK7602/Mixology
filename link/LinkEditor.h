#pragma once

#include "LinkProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

/** Mixology Link's small window: the track it sits on, its meters, the Trim knob, and whether Mixology
    can see it. Same look as Mixology (shared stylesheet, knobs and meters), in its own 3:2 canvas.

    C++ -> JS: "linkFrame" at 30 Hz. JS -> C++: native functions getLinkInfo(), setTrim(db). */
class LinkEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit LinkEditor (MixologyLinkProcessor&);
    ~LinkEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static bool isSupported();

    static constexpr int kDesignWidth = 600;
    static constexpr int kDesignHeight = 400;

private:
    void timerCallback() override;
    static juce::WebBrowserComponent::Options makeBaseOptions();
    juce::WebBrowserComponent::Options makeEditorOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    static juce::String savedTheme();

    MixologyLinkProcessor& processor;
    juce::WebBrowserComponent webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkEditor)
};
