#pragma once

#include "mixology/RackUnit.h"
#include "ProcHost.h"
#include "RackHost.h"

#include <string>

/** Sonicology Rack, stage 2: a request from a Rack view, applied to a Mixology Link's (or Mixology's own Full Mix)
    processing like a move of your own mouse. Message thread (rack::Publisher::serve's apply). Returns "" when done,
    else why not (the Rack shows it). */
namespace mixology::rackapply
{
    inline std::string apply (juce::AudioProcessorValueTreeState& apvts, const rack::Request& r, bool link)
    {
        using namespace mixology::proc;
        const auto param = [&apvts] (int index) { return rack::host::find (apvts, specs()[static_cast<std::size_t> (index)].id); };

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
        float value = 0.0f;
        if (! rackunit::fromKnob (r.control, r.value, value)) return rack::host::cannot ("Mixology", r);
        auto* p = rack::host::find (apvts, r.control);
        if (p == nullptr) return rack::host::cannot ("Mixology", r);
        rack::host::set (*p, r.gesture, assist || indexOf (r.control) < 0 ? value : sanitise (indexOf (r.control), value));
        return {};
    }
}
