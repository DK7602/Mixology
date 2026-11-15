#pragma once

#include "mixology/AssistEq.h"
#include "mixology/ProcParams.h"
#include "suite/RackLink.h"

#include <string>
#include <vector>

/** Sonicology Rack (docs/rack/RACK-PLAN.md): what a Mixology Link, or Mixology on the Full Mix, tells the Rack view about
    its modules. Framework-free and unit-tested; the plug-ins only fill a State from their parameters and meters.

    Modules in the order the sound goes through them: the Assist EQ (Links only), then Gate, EQ, Compressor, Multiband,
    Transient, Tone Balance, Loudness, Harmonics, Analog Touch, Clip Control, Width, Final Touch, Space, Limiter. Each
    with its state (on / off, all off while Processing is off), a one-line summary, up to eight of its controls as the
    view's knobs (named and written the way Mixology's pages show them), its EQ bands, its meter (gain reduction where it
    has one) and, for Space, the effects that play. */
namespace mixology::rackunit
{
    struct State
    {
        proc::Values values = proc::defaults();
        bool hasMeters = false;
        proc::Meters meters;
        bool link = true;                  // a Mixology Link (with its Assist EQ); false: Mixology on the Full Mix
        AssistSettings assist;             // Links
        bool assistByMixology = false;     // Mixology's Clean Up / Vocal Pocket set it
        std::string duckNames;             // the tracks its Space ducks under ("Lead Vox, Chorus Vox"), "" for none
    };

    /** The modules (brief: keys, states and summaries only; full: values, bands and meters too). */
    std::vector<rack::Module> modules (const State&, bool full);

    /** A control's value as Mixology's pages write it ("3.2 kHz", "−3.0 dB", "4.0:1", "Soft"), and where it sits on its
        knob (0..1). */
    std::string valueText (int index, float value);
    float valueNorm (int index, float value);
}
