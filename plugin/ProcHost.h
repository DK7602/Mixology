#pragma once

#include "mixology/ProcParams.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cmath>
#include <limits>

/** Milestone 5: the processing controls (mixology::proc's table) as host parameters. Shared by Mixology Link
    (each track) and Mixology (the full mix): same ids, ranges and texts in both. */
namespace mixology::proc::host
{
    inline juce::String valueText (const ParamSpec& s, float v)
    {
        const juce::String unit (s.unit);
        if (s.kind != Kind::number)
        {
            const auto names = juce::StringArray::fromTokens (s.choices, "|", "");
            return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))];
        }
        if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        if (unit == "dB") return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
        if (unit == "ms") return juce::String (v, v < 10.0f ? 2 : v < 100.0f ? 1 : 0) + " ms";
        if (unit == "%") return juce::String (juce::roundToInt (v)) + " %";
        if (unit == ":1") return juce::String (v, 1) + ":1";
        if (unit == "s") return juce::String (v, v < 10.0f ? 2 : 1) + " s";
        if (unit == "st") return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " st";
        return juce::String (v, 2);
    }

    /** The tail the host is told about. Cubase can stop a plug-in once its input has been silent for longer
        than its tail, which would cut a reverb off, so while Space is on the tail is generous (longer than any
        setting makes it) and endless for Freeze. Only switching Space on / off (or Freeze) changes it, so the
        host is rarely asked to look again (which restarts the plug-in). */
    inline double reportedTail (const Values& v)
    {
        if (! isOn (v, Module::space)) return 0.0;
        const bool freeze = juce::roundToInt (v[static_cast<std::size_t> (spType)]) == static_cast<int> (SpaceType::special)
                            && juce::roundToInt (v[static_cast<std::size_t> (spxMode)]) == static_cast<int> (SpecialMode::freeze);
        return freeze ? std::numeric_limits<double>::infinity() : 30.0;
    }

    /** True when the host should hear about a new tail. */
    inline bool tailChanged (double now, double told)
    {
        if (std::isinf (now) || std::isinf (told)) return std::isinf (now) != std::isinf (told);
        return std::abs (now - told) > 0.5;
    }

    /** Audio thread, first thing in a block: a sample that is not a number (NaN, infinity, from a broken plug-in
        before this one) would poison every filter it reaches and the loudness meters for good: it becomes silence. */
    template <typename Sample>
    void silenceNonFinite (juce::AudioBuffer<Sample>& buffer, int channels) noexcept
    {
        for (int c = 0; c < channels; ++c)
        {
            const auto* x = buffer.getReadPointer (c);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (x[i])) buffer.getWritePointer (c)[i] = Sample (0);
        }
    }

    inline float valueFromText (const ParamSpec& s, const juce::String& text)
    {
        if (s.kind != Kind::number)
        {
            const auto names = juce::StringArray::fromTokens (s.choices, "|", "");
            const int i = names.indexOf (text.trim(), true);
            return i >= 0 ? static_cast<float> (i) : text.getFloatValue();
        }
        float v = text.retainCharacters ("-+.0123456789").getFloatValue();
        if (text.containsIgnoreCase ("k")) v *= 1000.0f;
        return v;
    }

    /** Adds every processing control to a host parameter layout. */
    inline void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int versionHint)
    {
        for (const auto& s : specs())
        {
            const juce::ParameterID pid { s.id, versionHint };
            if (s.kind == Kind::toggle)
            {
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
            }
            else if (s.kind == Kind::choice)
            {
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, juce::StringArray::fromTokens (s.choices, "|", ""),
                                                                          juce::roundToInt (s.def)));
            }
            else
            {
                juce::NormalisableRange<float> range (s.min, s.max, s.step);
                if (s.curve == Curve::log) range.setSkewForCentre (std::sqrt (s.min * s.max));
                const auto* spec = &s;
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, s.name, range, s.def,
                    juce::AudioParameterFloatAttributes()
                        .withLabel (s.unit)
                        .withStringFromValueFunction ([spec] (float v, int) { return valueText (*spec, v); })
                        .withValueFromStringFunction ([spec] (const juce::String& t) { return valueFromText (*spec, t); })));
            }
        }
    }

    /** The parameters' raw values, read lock-free on the audio thread. */
    struct Raw
    {
        std::array<std::atomic<float>*, count> p {};

        void bind (juce::AudioProcessorValueTreeState& apvts)
        {
            for (int i = 0; i < count; ++i)
                p[static_cast<std::size_t> (i)] = apvts.getRawParameterValue (specs()[static_cast<std::size_t> (i)].id);
        }

        Values read() const noexcept
        {
            Values v {};
            for (std::size_t i = 0; i < p.size(); ++i) v[i] = p[i] != nullptr ? p[i]->load (std::memory_order_relaxed) : specs()[i].def;
            return v;
        }
    };

    /** Message thread: sets one control like a user move (the host sees it, the project saves it). */
    inline void set (juce::AudioProcessorValueTreeState& apvts, int index, float value)
    {
        if (index < 0 || index >= count) return;
        auto* p = apvts.getParameter (specs()[static_cast<std::size_t> (index)].id);
        if (p == nullptr) return;
        const auto n = p->convertTo0to1 (sanitise (index, value));
        if (std::abs (n - p->getValue()) < 1.0e-7f) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (n);
        p->endChangeGesture();
    }
}
