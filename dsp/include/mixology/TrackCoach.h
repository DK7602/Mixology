#pragma once

#include "MixPlanner.h"

#include <string>
#include <vector>

/** The Sonicology Rack's Coach on one of Mixology's tracks (Beta 28): what the plan from makeCoachPlan would do, as the
    Rack's COACH page shows it. The same shape as Voxology's Coach for a vocal (vox::coach::planJson), so the page draws
    both alike. Everything comes from what Analyze Mix (or the Coach's own listening) heard and the rules AUTO uses;
    the "after" tone is the track's measured spectrum with the plan's EQ and Tone Balance curves applied (the exact
    response of those filters), never guessed. */
namespace mixology::mix::coach
{
    /** How people name the role ("Bass / 808", "Lead vocal"). */
    const char* roleWord (Role);

    /** Tone regions the Coach names: band indices (kBands, 1/3 octave) of the region. */
    struct Region { const char* key; const char* name; int from, to; };
    const std::vector<Region>& regions();

    /** A region's level against the track's mids (640 Hz - 2 kHz), with the planner's 1.5 dB / octave tilt (a balanced
        track reads about flat). */
    double regionDb (const std::vector<double>& bands, int from, int to);

    /** The response (dB) of a Link's EQ and Tone Balance at `hz`, as `v` sets them (0 where they are off). */
    double toneResponseDb (const proc::Values& v, double hz);

    /** The plan as the Rack reads it (a JSON object): summary, heard, issues, the Link's modules with each setting's
        reason, bands (before / after, against the track's mids), results, moves, what it left alone. At most
        `maxBytes` (reasons shortened, then dropped; always valid JSON). */
    std::string planJson (const Plan&, const Report&, std::uint32_t hash, const std::vector<ChannelNow>&, std::size_t maxBytes);
}
