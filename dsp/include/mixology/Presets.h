#pragma once

#include "mixology/ProcParams.h"

#include <string>
#include <utility>
#include <vector>

/** Presets: a track's (or the Full Mix's) processing saved under a name, to load on any track of any song.

    - Mixology's own presets: starting points by kind of track (the lead vocal after Voxology, an 808, a kick...).
    - Yours: what you dialled in, saved from any track or the Full Mix (kept in one small text file, the same for
      every project).
    A preset holds the processing controls only (EQ, Dynamics, Tone, Stereo, Space...), never a track's Trim, its
    Assist EQ or what its Space ducks under. Loading one sets every control: the ones it doesn't name go to their
    defaults (off), so the result is the same whatever the track had before. Framework-free and unit-tested. */
namespace mixology::presets
{
    enum class Kind { track, fx, mix };   // a track's insert, an FX channel's return (Space 100 % wet), the Full Mix
    const char* kindKey (Kind);
    Kind kindFromKey (const std::string&);

    struct Preset
    {
        std::string name;
        std::string about;        // one line: what it does, when to use it
        Kind kind = Kind::track;
        std::string roles;        // the track roles it's made for, comma-separated ("vox", "keys,synth"); "" = any
        bool factory = false;     // Mixology's own (can't be changed or deleted)
        std::vector<std::pair<std::string, float>> values;   // control id -> value; the rest at their defaults

        bool suits (const std::string& roleKey) const;
    };

    /** Mixology's own presets. */
    const std::vector<Preset>& factory();

    /** Every control's value with this preset loaded (defaults + its values, each kept in range). */
    proc::Values valuesOf (const Preset&);
    /** What loading it on a target that has `current` sets: the preset's values, except that a track or mix preset keeps
        the target's own Space routing (an FX channel stays 100 % wet, a track stays a track). An FX preset makes it an
        FX return. */
    proc::Values forTarget (const Preset&, const proc::Values& current);
    /** The target's processing as a preset (only the controls that differ from their defaults). */
    Preset capture (const std::string& name, Kind, const std::string& roles, const proc::Values&);

    /** Your presets file: text, one block per preset. parse() skips what it doesn't know (a control a newer version
        added), so files move between versions. */
    std::string serialise (const std::vector<Preset>&);
    std::vector<Preset> parse (const std::string&);

    /** A name as it is stored: trimmed, one line, at most 60 characters. "" = not a usable name. */
    std::string cleanName (const std::string&);
}
