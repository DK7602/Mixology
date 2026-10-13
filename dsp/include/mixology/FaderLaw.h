#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

/** Maps Cubase's normalised fader position (0..1) to decibels and back.

    We never assume Cubase's fader curve: every time the bridge reports a position together with
    Cubase's own display text ("-6.02"), the pair is learned. Setting a dB value then interpolates
    between learned points; until enough points are known, a rough starting curve is used and the
    bridge refines the move from Cubase's readback (see BridgeSession). Learned points can be saved
    and loaded, since the curve is the same for every channel. */
namespace mixology
{
    class FaderLaw
    {
    public:
        static constexpr double kMinusInf = -1000.0;   // stands for -inf dB

        /** Parses Cubase's display text. Returns kMinusInf for "-oo", "-inf", "-∞";
            nothing when the text isn't a level. */
        static std::optional<double> parseDb (const std::string& text);

        void learn (double normalised, double db);
        double toDb (double normalised) const;
        double toNormalised (double db) const;

        std::size_t size() const { return points.size(); }
        void clear() { points.clear(); }

        /** "n:db;n:db;..." for saving with the plug-in state. */
        std::string serialise() const;
        void deserialise (const std::string&);

        /** Built-in guess used before anything is learned: +6 dB at the top, about 0 dB near 0.78. */
        static double guessDb (double normalised);
        static double guessNormalised (double db);

    private:
        // Key: position quantised to the protocol's 21-bit grid. Value: dB.
        std::map<long, double> points;
        static long key (double normalised);
    };
}
