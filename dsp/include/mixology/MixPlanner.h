#pragma once

#include "mixology/AssistEq.h"
#include "mixology/MixAnalysis.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/** Milestone 4: turns Analyze Mix's report into concrete moves that you look over before anything changes.

    - Balance: fader levels. Instruments against each other (role targets per genre; only clear outliers
      move), the lead vocal against the rest of the mix (the style's target, or the level you set), ad-libs
      under the vocal, all at the same overall loudness. Fader moves since Analyze Mix are allowed for, so
      applying a plan and opening it again finds nothing more to do. Tracks of one kind move
      together, so their balance among themselves stays yours. Room for Masterology (mix peaks around
      -6 dBTP) is offered separately: every fader down together. With a music group (a group channel the
      instruments play through, e.g. Voxology's "Beat"), the music moves on that one fader.
    - Stereo Architect: pans. Low end and the lead vocal in the middle, doubles and pairs spread left / right,
      a lopsided mix evened out.
    - Clean Up: Assist EQ low cuts on tracks with rumble (and on vocals), mud dips where the mix is muddy.
    - Vocal Pocket: Assist EQ dips where instruments cover the vocal's words.
    - Auto Mix: all four together.

    Nothing here touches Cubase: the plug-in applies the plan you approve (faders and pans through the bridge,
    the Assist EQ through each track's Mixology Link). Framework-free and unit-tested. */
namespace mixology::mix
{
    /** One Cubase channel as Mixology sees it right now (the bridge's fader scan and the Links). */
    struct ChannelNow
    {
        std::uint32_t hash = 0;
        std::string name;
        bool faderKnown = false;
        double faderDb = 0.0;
        bool hasPan = false;
        double pan = 0.0;          // -100 = L100, 0 = centre, +100 = R100
        bool panUnknown = false;   // a stereo track on Cubase's Combined Panner: Cubase only reports its left edge
        int channels = 0;          // its Mixology Link's channels (1 mono, 2 stereo, 0 unknown): only mono tracks get panned
        bool hasFaderAtAnalysis = false;   // the fader when Analyze Mix listened: moves since then are allowed for
        double faderAtAnalysis = 0.0;      // (so applying a plan and opening it again doesn't repeat its moves)
        bool musicBus = false;     // the music group you marked: the instruments play through it
        bool locked = false;       // protected (padlock)
        bool hasLink = false;      // a Mixology Link sits on it (its Assist EQ can be set)
        AssistSettings assist;     // that Link's Assist EQ now
    };

    enum class Tool { autoMix, balance, stereo, cleanUp, pocket };
    const char* toolKey (Tool);
    const char* toolName (Tool);
    std::optional<Tool> toolFromKey (const std::string&);

    struct Change
    {
        std::uint32_t hash = 0;
        std::string name;
        Role role = Role::other;
        std::string kind;          // "level" (dB), "pan" (-100..+100), "lowcut" (Hz, 0 = off), "mud" / "pocket" (dB)
        double from = 0.0, to = 0.0;
        double wanted = 0.0;       // levels: the whole move's target, before the 6 dB limit
        bool big = false;          // levels: the whole move is more than 6 dB (Apply stops at 6 unless you allow more)
        double hz = 0.0;           // mud / pocket: where the dip sits
        std::string why;
    };

    struct Plan
    {
        Tool tool = Tool::autoMix;
        bool valid = false;        // false: Analyze Mix hasn't run (summary says what to do)
        std::string title;
        std::string summary;
        std::vector<Change> changes;
        std::vector<std::string> notes;   // what it left alone, and why

        /** Room for Masterology, offered on its own (you tick it): every fader that plays moves headroomDb
            (< 0) together, on top of the level changes. 0 = not needed. */
        struct HeadroomMove
        {
            std::uint32_t hash = 0;
            std::string name;
            double from = 0.0;        // the fader now
            double afterLevels = 0.0; // after this plan's level change (if any), before the headroom move
        };
        double headroomDb = 0.0;
        std::string headroomWhy;
        std::vector<HeadroomMove> headroom;

        std::vector<std::string> groupHints;   // channels that look like a group the tracks play through (ask: music group?)
    };

    constexpr double kMaxMoveDb = 6.0;
    constexpr double kMaxFaderDb = 6.0;   // top of a Cubase channel fader

    Plan makePlan (Tool, const Report&, const std::vector<ChannelNow>&);

    /** "Gtr L" / "Gtr R", "Vocals" / "Vocals (D)", "Keys 1" / "Keys 2": the shared name, and which side the
        name asks for (-1 left, +1 right, 0 none) and its order among the takes (0 = the original). */
    struct PairName
    {
        std::string base;
        int side = 0;
        int order = 0;
        bool explicitPair = false;   // L / R / double / (D): clearly one of a pair, whatever its role
    };
    PairName pairName (const std::string& name);
}
