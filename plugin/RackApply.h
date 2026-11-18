#pragma once

#include "mixology/RackUnit.h"
#include "ProcHost.h"
#include "RackHost.h"

#include <atomic>
#include <cstdint>
#include <string>

/** Sonicology Rack, stage 2: a request from a Rack view, applied to a Mixology Link's (or Mixology's own Full Mix)
    processing like a move of your own mouse. Message thread (rack::Publisher::serve's apply). Returns "" when done,
    else why not (the Rack shows it). */
namespace mixology::rackapply
{
    /** Beta 33: the processing modules' order (proc::Order), kept in the processing state's "order" property (saved with
        the project; none: Mixology's own) and in `packed` (proc::packOrder; the audio thread hands it to the chain).
        Message thread. */
    inline void setOrder (juce::AudioProcessorValueTreeState& apvts, std::atomic<std::uint64_t>& packed, const proc::Order& o)
    {
        const auto valid = proc::validOrder (o) ? o : proc::kDefaultOrder;
        if (proc::packOrder (valid) == packed.load()) return;
        packed.store (proc::packOrder (valid));
        if (valid == proc::kDefaultOrder) apvts.state.removeProperty ("order", nullptr);
        else apvts.state.setProperty ("order", juce::String (proc::orderText (valid)), nullptr);
        apvts.processor.updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
    }
    /** After a state was loaded (replaceState): the order it has (a project from before it: Mixology's own). */
    inline void loadOrder (const juce::AudioProcessorValueTreeState& apvts, std::atomic<std::uint64_t>& packed)
    {
        packed.store (proc::packOrder (proc::orderFromText (apvts.state.getProperty ("order").toString().toStdString())));
    }

    /** `order`: where the chain's order lives (none: this plug-in can't move modules). */
    inline std::string apply (juce::AudioProcessorValueTreeState& apvts, const rack::Request& r, bool link,
                              std::atomic<std::uint64_t>* order = nullptr)
    {
        using namespace mixology::proc;
        const auto param = [&apvts] (int index) { return rack::host::find (apvts, specs()[static_cast<std::size_t> (index)].id); };

        if (r.change == rack::Change::order)
        {
            // Beta 33: a module moved in the Rack (control: its key; value: its place among the movable ones), or RESET.
            if (order == nullptr) return rack::host::cannot ("Mixology", r);
            if (r.control == "reset") { setOrder (apvts, *order, kDefaultOrder); return {}; }
            Module m = Module::eq;
            if (! rackunit::moduleOf (r.control, m) || ! isMovable (m))
                return "That module stays where it is: the Gate comes first; Final Touch, Space and the Limiter come last.";
            setOrder (apvts, *order, movedTo (unpackOrder (order->load()), m, static_cast<int> (std::lround (r.value))));
            return {};
        }

        if (r.change == rack::Change::save) return "Only Voxology saves chains from the Rack.";
        if (r.change == rack::Change::power)
        {
            // The Rack's BYPASS: this track's (or the Full Mix's) processing off / on, as Mixology's own ON switch.
            auto* chain = param (chainOn);
            if (chain == nullptr) return rack::host::cannot ("Mixology", r);
            rack::host::setSwitch (*chain, r.value >= 0.5f);
            return {};
        }
        if (r.change == rack::Change::state)
        {
            const int sw = rackunit::switchOf (r.module);
            if (sw < 0)
                return r.module == "mix.assist" ? "The Assist EQ has no on switch: it is on while one of its knobs is up."
                                                : rack::host::cannot ("Mixology", r);
            auto* p = param (sw);
            auto* chain = param (chainOn);
            if (p == nullptr || chain == nullptr) return rack::host::cannot ("Mixology", r);
            const bool on = r.value >= 0.5f;
            if (on && chain->getValue() < 0.5f)
            {
                // Processing is off here: switching it on would also start every module left switched on. Only when
                // none is (a new Link) does the Rack switch it on with this one.
                for (int m = 0; m < kModules; ++m)
                {
                    const int other = onParam (static_cast<Module> (m));
                    if (other == sw) continue;
                    if (auto* o = param (other); o != nullptr && o->getValue() >= 0.5f)
                        return "Mixology's processing is off on this track. Turn it on in Mixology (it would start the other modules too).";
                }
                rack::host::setSwitch (*chain, true);
            }
            rack::host::setSwitch (*p, on);
            return {};
        }

        const bool assist = r.control == rackunit::kLowCutKey || r.control == rackunit::kMudKey || r.control == rackunit::kPocketKey;
        if (assist && ! link) return rack::host::cannot ("Mixology", r);
        if (r.change == rack::Change::reset)
        {
            auto* p = assist || indexOf (r.control) >= 0 ? rack::host::find (apvts, r.control) : nullptr;
            if (p == nullptr) return rack::host::cannot ("Mixology", r);
            rack::host::setDefault (*p);
            return {};
        }
        float value = 0.0f;
        if (! rackunit::fromKnob (r.control, r.value, value)) return rack::host::cannot ("Mixology", r);
        auto* p = rack::host::find (apvts, r.control);
        if (p == nullptr) return rack::host::cannot ("Mixology", r);
        rack::host::set (*p, r.gesture, assist || indexOf (r.control) < 0 ? value : sanitise (indexOf (r.control), value));
        return {};
    }
}
