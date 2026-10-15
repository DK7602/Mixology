#include "mixology/SetupCheck.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mixology::setup
{
    namespace
    {
        std::string quoted (const std::string& s) { return "'" + s + "'"; }

        std::string joinQuoted (const std::vector<std::string>& names, std::size_t most)
        {
            std::string out;
            const std::size_t n = std::min (names.size(), most);
            for (std::size_t i = 0; i < n; ++i)
                out += (i == 0 ? "" : i + 1 == n ? " and " : ", ") + quoted (names[i]);
            if (names.size() > n) out += " and " + std::to_string (names.size() - n) + " more";
            return out;
        }

        double energy (double db) { return db <= kSilentDb + 1.0 ? 0.0 : std::pow (10.0, db / 10.0); }
    }

    double panWidth (double pan)
    {
        // Constant-power pan: left = cos, right = sin of theta (0 .. pi/2). Side share = (1 - sin 2 theta) / 2.
        const double theta = (std::clamp (pan, -100.0, 100.0) + 100.0) / 200.0 * (3.14159265358979323846 / 2.0);
        return std::max (0.0, (1.0 - std::sin (2.0 * theta)) / 2.0);
    }

    void MonoWatch::reset()
    {
        moments.clear();
        evidenceNames.clear();
        lastExpected = 0.0;
    }

    void MonoWatch::add (const MixNow& mix, const std::vector<TrackNow>& tracks)
    {
        if (! mix.playing || mix.loudness < -60.0f) return;
        const double mixEnergy = energy (mix.loudness);
        if (mixEnergy <= 0.0) return;

        // The side share the tracks should give the mix if nothing folds them: each track's energy (after its fader)
        // times its own width, or the width its pan gives a mono track. Over the mix's energy (channels without a
        // Link only add to it, so this errs on the low side).
        double side = 0.0;
        std::vector<std::pair<double, std::string>> contributors;
        for (const auto& t : tracks)
        {
            if (! t.audible || t.loudness < -60.0f) continue;
            const double e = energy (t.loudness + t.faderDb);
            double share = std::clamp (static_cast<double> (t.width), 0.0, 0.5);
            if (t.panKnown && t.panWorks) share = std::max (share, panWidth (t.pan));
            if (share <= 0.0) continue;
            side += e * share;
            contributors.push_back ({ e * share, t.name });
        }
        const double expected = std::min (0.5, side / mixEnergy);
        if (expected < kExpectedWidth) return;   // the tracks are (nearly) mono themselves: nothing to judge

        lastExpected = expected;
        const bool mono = mix.width <= kMonoWidth && mix.correlation >= 0.998f;
        moments.push_back (mono);
        while (static_cast<int> (moments.size()) > kWindow) moments.pop_front();
        if (mono)
        {
            std::sort (contributors.begin(), contributors.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
            evidenceNames.clear();
            for (std::size_t i = 0; i < contributors.size() && i < 3; ++i) evidenceNames.push_back (contributors[i].second);
        }
    }

    bool MonoWatch::monoFold() const
    {
        if (static_cast<int> (moments.size()) < kEnough) return false;
        // The most recent moments decide (a fix shows within a few seconds): 90 % of the last kEnough were mono.
        const auto from = moments.end() - kEnough;
        const auto mono = std::count (from, moments.end(), true);
        return mono >= (kEnough * 9) / 10;
    }

    std::vector<mix::Suggestion> issues (const Context& c)
    {
        std::vector<mix::Suggestion> out;

        if (c.mixChannels == 1)
        {
            mix::Suggestion s;
            s.id = "setup-mono-mixology";
            s.kind = "warn";
            s.icon = "mono";
            s.title = "Mixology is on a mono channel";
            s.text = "Mixology sits on a channel with one audio channel, so it hears your whole mix in mono: no width, no pans, "
                     "and its stereo advice can't work.";
            s.need = "Yes: Mixology belongs on the {daw:master} channel, which is stereo.";
            s.steps = { "Remove Mixology from this channel.",
                        "Put it on the {daw:master} channel, before Masterology: {daw:loadOnMaster}.",
                        "If the {daw:master} channel itself is mono: {daw:outputsStereo}." };
            out.push_back (std::move (s));
        }
        else if (c.monoFold)
        {
            mix::Suggestion s;
            s.id = "setup-mono-group";
            s.kind = "warn";
            s.icon = "mono";
            s.title = "Your mix arrives in mono";
            const std::string evidence = c.stereoTracks.empty() ? std::string ("Your tracks play in stereo")
                                                                 : "Your tracks play in stereo (for example " + joinQuoted (c.stereoTracks, 3) + ")";
            s.text = evidence + ", but at the {daw:master} channel left and right are exactly the same: every pan and every stereo sound is "
                     "folded to the middle on the way. ";
            if (! c.busCandidates.empty())
                s.text += "Most likely " + quoted (c.busCandidates.front()) + " is a mono group: everything that plays through it comes out in the middle.";
            else
                s.text += "Something between them and the {daw:master} channel is mono: usually a group channel they play through.";
            s.need = "Yes, unless the song is meant to be mono: width and pans are half of a modern mix.";
            const std::string where = c.busCandidates.empty() ? std::string ("each group channel your tracks play through")
                                                              : quoted (c.busCandidates.front());
            s.steps = { "Find " + where + " in {daw:mixer}: {daw:monoBusFix}.",
                        "Check the other groups and FX channels the tracks play through the same way, and the {daw:master} channel "
                        "({daw:outputsStereo}).",
                        "Press play: the Stereo page's scope opens up. Then run Analyze Mix again." };
            s.tracks = c.busCandidates;
            if (s.tracks.size() > 3) s.tracks.resize (3);
            out.push_back (std::move (s));
        }

        if (! c.vocalsWithoutLink.empty())
        {
            mix::Suggestion s;
            s.id = "setup-vocal-link";
            s.kind = "warn";
            s.icon = "link";
            const auto n = c.vocalsWithoutLink.size();
            s.title = n == 1 ? quoted (c.vocalsWithoutLink.front()) + " has no Mixology Link"
                             : std::to_string (n) + " vocals have no Mixology Link";
            s.text = (n == 1 ? quoted (c.vocalsWithoutLink.front()) + " has Voxology on it but no Mixology Link"
                             : joinQuoted (c.vocalsWithoutLink, 4) + " have Voxology on them but no Mixology Link")
                     + ", so Mixology can't hear " + (n == 1 ? "it" : "them") + " on " + (n == 1 ? "its" : "their")
                     + " own: Analyze Mix and Auto Mix can't set the vocal level against the beat or polish " + (n == 1 ? "it." : "them.");
            s.need = "Yes: the vocal is the most important part of the mix.";
            s.steps = { "Add Mixology Link to each of " + std::string (n == 1 ? "it" : "them") + " {daw:linkAfterVoxology}.",
                        "{daw:manyAtOnce}.",
                        "Then run Analyze Mix again." };
            s.tracks = c.vocalsWithoutLink;
            out.push_back (std::move (s));
        }
        return out;
    }
}
