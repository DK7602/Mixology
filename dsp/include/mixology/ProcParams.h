#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

/** Milestone 5: the processing every Mixology Link runs on its track (and Mixology on the full mix).

    One table describes every control: its id (what the host saves and automates, and what the interface
    sends), its range, default and unit. The Link and Mixology build their host parameters from it, the
    registry carries values by their index in it, and the interface gets a copy (plugin/ui/procspec.js,
    generated from this table and checked by a test). Every module starts switched off, so a new Link
    changes nothing until you turn something on.

    Never reorder or rename released ids: projects store them. New controls go at the end. */
namespace mixology::proc
{
    enum class Kind : std::uint8_t { number, toggle, choice };
    enum class Curve : std::uint8_t { linear, log };   // log: knob and glide in octaves / ratios

    struct ParamSpec
    {
        const char* id;
        const char* name;      // as the host shows it
        Kind kind;
        float min, max, def;
        Curve curve;
        float step;            // 0 = continuous
        const char* unit;      // "dB", "Hz", "ms", "%", ":1" or ""
        const char* choices;   // choice names, "A|B|C"
    };

    // clang-format off
    enum Id : int
    {
        chainOn,
        // EQ: six bands, each one of five shapes
        eqOn, eqMode,
        eq1Type, eq1Freq, eq1Gain, eq1Q,
        eq2Type, eq2Freq, eq2Gain, eq2Q,
        eq3Type, eq3Freq, eq3Gain, eq3Q,
        eq4Type, eq4Freq, eq4Gain, eq4Q,
        eq5Type, eq5Freq, eq5Gain, eq5Q,
        eq6Type, eq6Freq, eq6Gain, eq6Q,
        // Dynamics
        compOn, compThr, compRatio, compAtt, compRel, compMakeup, compKnee,
        multiOn, multiX1, multiX2, multiLow, multiMid, multiHigh, multiBands,
        transOn, transAttack, transSustain, transSpeed, transOut, transMix, transFocus,
        gateOn, gateThr, gateRange, gateAtt, gateHold, gateRel, gateMode,
        limOn, limIn, limCeil, limRel, limLook, limChar, limDetect,
        // Tone
        tbOn, tbWarmth, tbPresence, tbAir, tbGlue, tbPunch,
        ldOn, ldTrim, ldDensity, ldLow, ldTop,
        clipOn, clipThr, clipSoft, clipOs, clipMix,
        harmOn, harmDrive, harmEvenOdd, harmTone, harmMix,
        anOn, anTape, anConsole, anNoise, anMix,
        finOn, finWidth, finAir, finGlue, finOut,
        // Stereo
        stOn, stWidth, stLow, stMid, stHigh, stLowX, stHighX,
        // Space (round 2): its effects, their wet sound filtered and ducked together (spaceLayers() below)
        spOn, spType, spRoute, spLowCut, spHighCut, duckAmount,
        revPre, revDecay, revDamp, revWidth, revMix,
        dlyTime, dlyFeedback, dlyTone, dlyPingPong, dlyMix,
        choRate, choDepth, choDelay, choWidth, choMix,
        phaRate, phaDepth, phaFeedback, phaCentre, phaMix,
        hallPre, hallDecay, hallSize, hallDamp, hallMix,
        roomPre, roomDecay, roomSize, roomLate, roomMix,
        spxShift, spxFeedback, spxMotion, spxTone, spxMix, spxMode,
        spLevel,   // v0.7.1: the effect's level on an FX channel (there MIX doesn't apply: only the effect comes out)
        // Space layers: each effect's own on switch, in SpaceType order (revOn + type). See spaceLayers().
        revOn, dlyOn, choOn, phaOn, hallOn, roomOn, spxOn,
        count
    };
    enum class SpaceType : int { reverb = 0, delay = 1, chorus = 2, phaser = 3, hall = 4, room = 5, special = 6 };
    enum class SpecialMode : int { shimmer = 0, drift = 1, swell = 2, freeze = 3 };
    constexpr int kSpaceTypes = 7;
    /** The Delay's Time choices, in beats (quarter notes). */
    constexpr double kDelayBeats[] = { 0.125, 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };
    // clang-format on

    constexpr int kEqBands = 6;
    enum class EqShape : int { bell = 0, lowShelf = 1, highShelf = 2, lowCut = 3, highCut = 4 };
    enum class EqMode : int { precision = 0, smooth = 1, natural = 2 };
    /** Band b (0..5): its type / freq / gain / Q parameter. */
    constexpr int eqType (int b) { return eq1Type + 4 * b; }
    constexpr int eqFreq (int b) { return eq1Freq + 4 * b; }
    constexpr int eqGain (int b) { return eq1Gain + 4 * b; }
    constexpr int eqQ (int b) { return eq1Q + 4 * b; }

    /** The processing modules, in the order the audio runs through them. */
    /** (Space is last in this list but runs before the Limiter: see ProcChain.) */
    enum class Module : int { gate, eq, comp, multi, trans, tone, loud, harm, analog, clip, stereo, final, limit, space, count };
    constexpr int kModules = static_cast<int> (Module::count);
    int onParam (Module);                 // its on / off switch
    const char* moduleKey (Module);       // "gate", "eq", ... (the interface's module ids)

    using Values = std::array<float, count>;

    /** Space layers: up to three of the seven effects at once on one track, each with its own on switch
        (revOn + type) and its own knobs (its Mix too). They always run in this order: Chorus and Phaser first, one
        after the other (each blended in by its Mix), then Delay, Reverb, Hall, Room and Special FX side by side on
        that sound, each scaled by its Mix, added up. The Effect EQ and Ducking work on all of their wet sound. */
    constexpr int kMaxSpaceEffects = 3;
    constexpr SpaceType kSpaceOrder[kSpaceTypes] = { SpaceType::chorus, SpaceType::phaser, SpaceType::delay, SpaceType::reverb,
                                                     SpaceType::hall, SpaceType::room, SpaceType::special };
    constexpr int effectOn (SpaceType t) { return revOn + static_cast<int> (t); }
    constexpr std::uint32_t bitOf (SpaceType t) { return 1u << static_cast<int> (t); }
    constexpr bool isModulation (SpaceType t) { return t == SpaceType::chorus || t == SpaceType::phaser; }
    /** The effects that make a space of their own (a room around the sound): two of them at once can wash it out. */
    constexpr bool isReverbType (SpaceType t) { return t == SpaceType::reverb || t == SpaceType::hall || t == SpaceType::room || t == SpaceType::special; }
    /** The effects Space plays (bit per SpaceType), whether Space is on or not: those switched on, at most three
        (the first three in the order above, so automation or a damaged project can't run all seven). None switched
        on (a project, preset or plan from before layers) = Space Effect's (spType) alone, as it always was. */
    std::uint32_t spaceLayers (const Values&);
    /** What is playing: spaceLayers() while Space (and the processing) is on, else 0. */
    std::uint32_t spaceEffects (const Values&);
    /** How many effects, and how many of them reverb types, in a set of bits. */
    int effectCount (std::uint32_t bits);
    int reverbTypeCount (std::uint32_t bits);
    /** Their names in the order they run ("Phaser + Hall"); "" for none. */
    std::string effectNames (std::uint32_t bits, const char* joiner = " + ");
    /** The controls that make exactly these effects play (every on switch, Space on when any, Space Effect = `main`
        if it is one of them, else the first): what plans and presets write, so nothing switched on before stays. */
    std::array<std::pair<int, float>, kSpaceTypes + 2> spaceControls (std::uint32_t bits, SpaceType main);

    const std::array<ParamSpec, count>& specs();
    int indexOf (std::string_view id);    // -1 when there is no such control
    Values defaults();
    /** Inside the range, on the step, a whole number for toggles and choices. NaN gives the default. */
    float sanitise (int index, float value);
    bool isOn (const Values&, Module);    // switched on (and the chain too)

    struct Meters
    {
        float inL = -100.0f, inR = -100.0f, outL = -100.0f, outR = -100.0f;   // peaks, dBFS
        float comp = 0.0f, multiLow = 0.0f, multiMid = 0.0f, multiHigh = 0.0f;  // dB taken off
        float trans = 0.0f;                                                     // dB, + louder / - quieter (largest move)
        float gate = 0.0f, limit = 0.0f, clip = 0.0f, glue = 0.0f;              // dB taken off (clip: dB over the threshold)
        float spaceWet = -100.0f, duck = 0.0f;                                  // the Space effect's level (dBFS peak), dB ducked
        std::uint32_t running = 0;                                              // bit per Module: it is processing
    };
    constexpr int kMeterValues = 15;   // the floats in Meters, in order (how the registry carries them)
    void meterValues (const Meters&, float* out);
    Meters metersFrom (const float* values, std::uint32_t running);
}
