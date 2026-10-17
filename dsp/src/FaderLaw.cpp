#include "mixology/FaderLaw.h"

#include "mixology/BridgeProtocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace mixology
{
    std::optional<double> FaderLaw::parseDb (const std::string& raw)
    {
        std::string t;
        for (unsigned char c : raw)
            if (! std::isspace (c)) t.push_back (static_cast<char> (std::tolower (c)));
        if (t.empty()) return std::nullopt;

        if (t.find ("oo") != std::string::npos || t.find ("inf") != std::string::npos
            || t.find ("\xE2\x88\x9E") != std::string::npos)   // U+221E infinity sign
            return kMinusInf;

        // Unicode minus (U+2212) -> '-'
        for (std::size_t p; (p = t.find ("\xE2\x88\x92")) != std::string::npos;)
            t.replace (p, 3, "-");
        if (t.size() >= 2 && t.compare (t.size() - 2, 2, "db") == 0)
            t.erase (t.size() - 2);
        std::replace (t.begin(), t.end(), ',', '.');

        char* end = nullptr;
        const double v = std::strtod (t.c_str(), &end);
        if (end == t.c_str() || *end != '\0' || ! std::isfinite (v)) return std::nullopt;
        return v;
    }

    long FaderLaw::key (double n)
    {
        return std::lround (std::clamp (n, 0.0, 1.0) * bridge::kValueMax);
    }

    void FaderLaw::learn (double normalised, double db)
    {
        if (! std::isfinite (normalised) || ! std::isfinite (db)) return;
        db = std::max (db, kMinusInf);
        const long k = key (normalised);

        // The curve only rises. A new reading wins over older points that contradict it (a stale pair
        // from a missed update must never bend the curve for good).
        for (auto it = points.begin(); it != points.end();)
        {
            const bool contradicts = (it->first < k && it->second > db + 0.01) || (it->first > k && it->second < db - 0.01);
            it = contradicts ? points.erase (it) : std::next (it);
        }
        points[k] = db;
    }

    double FaderLaw::guessDb (double n)
    {
        n = std::clamp (n, 0.0, 1.0);
        if (n <= 1.0e-6) return kMinusInf;
        return std::max (kMinusInf, 6.0 + 60.0 * std::log10 (n));
    }

    double FaderLaw::guessNormalised (double db)
    {
        if (db <= kMinusInf + 1.0) return 0.0;
        return std::clamp (std::pow (10.0, (db - 6.0) / 60.0), 0.0, 1.0);
    }

    double FaderLaw::toDb (double n) const
    {
        const long k = key (n);
        if (auto it = points.find (k); it != points.end()) return it->second;

        // The learned points with a real dB value (minus infinity says nothing about the curve's shape).
        const std::pair<const long, double>* lo = nullptr;
        const std::pair<const long, double>* hi = nullptr;
        const std::pair<const long, double>* first = nullptr;
        const std::pair<const long, double>* last = nullptr;
        for (const auto& p : points)
        {
            if (p.second <= kMinusInf) continue;
            if (first == nullptr) first = &p;
            last = &p;
            if (p.first < k) lo = &p;
            else if (hi == nullptr) hi = &p;
        }
        if (first == nullptr) return guessDb (n);
        if (lo != nullptr && hi != nullptr)
        {
            const double t = static_cast<double> (k - lo->first) / static_cast<double> (hi->first - lo->first);
            return lo->second + t * (hi->second - lo->second);
        }

        // Outside the learned range: the built-in curve shifted through the nearest learned point, as toNormalised
        // does, so a dB value turned into a fader position and back stays the same.
        const auto* nearest = lo != nullptr ? last : first;
        const double guess = guessDb (n);
        if (guess <= kMinusInf) return kMinusInf;
        const double offset = nearest->second - guessDb (static_cast<double> (nearest->first) / bridge::kValueMax);
        return std::max (kMinusInf, guess + offset);
    }

    double FaderLaw::toNormalised (double db) const
    {
        if (db <= kMinusInf + 1.0) return 0.0;

        // Learned points are monotonic in practice; find the pair whose dB values bracket the target.
        const std::pair<const long, double>* below = nullptr;
        const std::pair<const long, double>* above = nullptr;
        for (const auto& p : points)
        {
            if (p.second <= kMinusInf) continue;
            if (p.second <= db && (below == nullptr || p.second >= below->second)) below = &p;
            if (p.second >= db && (above == nullptr || p.second <= above->second)) above = &p;
        }

        const auto norm = [] (long k) { return static_cast<double> (k) / bridge::kValueMax; };
        if (below != nullptr && above != nullptr)
        {
            if (above->second - below->second < 1.0e-9) return norm (below->first);
            const double t = (db - below->second) / (above->second - below->second);
            return norm (below->first) + t * (norm (above->first) - norm (below->first));
        }

        // Outside the learned range: shift the built-in curve so it passes through the nearest learned point.
        if (const auto* nearest = below != nullptr ? below : above)
        {
            const double offset = nearest->second - guessDb (norm (nearest->first));
            return guessNormalised (db - offset);
        }
        return guessNormalised (db);
    }

    std::string FaderLaw::serialise() const
    {
        std::ostringstream s;
        s.precision (10);
        bool first = true;
        for (const auto& [k, db] : points)
        {
            if (! first) s << ';';
            first = false;
            s << k << ':' << db;
        }
        return s.str();
    }

    void FaderLaw::deserialise (const std::string& text)
    {
        points.clear();
        std::istringstream s (text);
        std::string item;
        while (std::getline (s, item, ';'))
        {
            const auto colon = item.find (':');
            if (colon == std::string::npos) continue;
            char* end = nullptr;
            const long k = std::strtol (item.c_str(), &end, 10);
            const double db = std::strtod (item.c_str() + colon + 1, nullptr);
            if (k >= 0 && k <= static_cast<long> (bridge::kValueMax) && std::isfinite (db))
                points[k] = std::max (db, kMinusInf);
        }
    }
}
