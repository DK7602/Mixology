#include "mixology/RackUnit.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace mixology::rackunit
{
    namespace
    {
        using namespace mixology::proc;
        const char* const kMinus = "\xE2\x88\x92";   // −

        // Fixed decimals without printf (whose decimal point follows the host's locale).
        std::string fixed (double v, int decimals)
        {
            if (! std::isfinite (v)) v = 0.0;
            long long scale = 1;
            for (int i = 0; i < decimals; ++i) scale *= 10;
            const long long n = std::llround (std::abs (v) * static_cast<double> (scale));
            std::string s = std::to_string (n / scale);
            if (decimals > 0)
            {
                std::string frac = std::to_string (n % scale);
                s += "." + std::string (static_cast<std::size_t> (decimals) - std::min (frac.size(), static_cast<std::size_t> (decimals)), '0') + frac;
            }
            return (v < 0.0 && n != 0 ? std::string (kMinus) : std::string()) + s;
        }

        std::string signedDb (double db, int decimals = 1)
        {
            return (db > 0.0 && std::llround (db * std::pow (10.0, decimals)) != 0 ? "+" : "") + fixed (db, decimals) + " dB";
        }

        std::string hzText (double hz)
        {
            if (hz >= 1000.0) return fixed (hz / 1000.0, hz >= 10000.0 ? 1 : (std::fmod (hz, 1000.0) < 1.0 ? 0 : 1)) + " kHz";
            return fixed (hz, 0) + " Hz";
        }

        std::string choiceName (const ParamSpec& s, int i)
        {
            std::string all = s.choices != nullptr ? s.choices : "";
            int k = 0;
            std::size_t start = 0;
            for (std::size_t p = 0; p <= all.size(); ++p)
                if (p == all.size() || all[p] == '|')
                {
                    if (k++ == i) return all.substr (start, p - start);
                    start = p + 1;
                }
            return std::to_string (i);
        }

        int stepsOf (const ParamSpec& s)
        {
            return s.kind == Kind::number ? 0 : static_cast<int> (std::lround (s.max - s.min)) + 1;
        }

        // A knob the Rack can turn: named by its control's id (what the host saves and automates).
        rack::Value knob (int index, std::string label, std::string text, float value, bool bi)
        {
            const auto& s = specs()[static_cast<std::size_t> (index)];
            return { std::move (label), std::move (text), valueNorm (index, value), bi, s.id, stepsOf (s) };
        }

        rack::Value value (const State& st, int index, const char* label)
        {
            const float v = st.values[static_cast<std::size_t> (index)];
            const auto& s = specs()[static_cast<std::size_t> (index)];
            return knob (index, label, valueText (index, v), v, s.kind == Kind::number && s.min < 0.0f && s.max > 0.0f);
        }

        void values (rack::Module& m, const State& st, std::initializer_list<std::pair<int, const char*>> list)
        {
            for (const auto& [index, label] : list)
                if (m.values.size() < static_cast<std::size_t> (rack::kMaxValues)) m.values.push_back (value (st, index, label));
        }

        // The summary: its first controls, "Ratio 4.0:1 · Threshold −18.0 dB".
        std::string summaryOf (const rack::Module& m, std::size_t count = 2)
        {
            std::string s;
            for (std::size_t i = 0; i < std::min (count, m.values.size()); ++i)
                s += (s.empty() ? "" : " \xC2\xB7 ") + m.values[i].name + " " + m.values[i].text;
            return s;
        }

        float v (const State& st, int index) { return st.values[static_cast<std::size_t> (index)]; }

        rack::BandType bandType (int shape)
        {
            switch (static_cast<EqShape> (shape))
            {
                case EqShape::lowShelf:  return rack::BandType::lowShelf;
                case EqShape::highShelf: return rack::BandType::highShelf;
                case EqShape::lowCut:    return rack::BandType::lowCut;
                case EqShape::highCut:   return rack::BandType::highCut;
                case EqShape::bell:      break;
            }
            return rack::BandType::bell;
        }

        const char* key (Module m)
        {
            switch (m)
            {
                case Module::gate:   return "mix.gate";
                case Module::eq:     return "mix.eq";
                case Module::comp:   return "mix.comp";
                case Module::multi:  return "mix.multi";
                case Module::trans:  return "mix.trans";
                case Module::tone:   return "mix.tone";
                case Module::loud:   return "mix.loud";
                case Module::harm:   return "mix.harm";
                case Module::analog: return "mix.analog";
                case Module::clip:   return "mix.clip";
                case Module::stereo: return "mix.stereo";
                case Module::final:  return "mix.final";
                case Module::space:  return "mix.space";
                case Module::limit:  return "mix.limit";
                case Module::count:  break;
            }
            return "mix.eq";
        }

        void gr (rack::Module& m, const State& st, float takenOff)
        {
            m.meterKind = rack::MeterKind::gr;
            m.meter = st.hasMeters ? -std::max (0.0f, takenOff) : 0.0f;
        }
    }

    float assistNorm (const std::string& key, float value)
    {
        if (key == kLowCutKey)
            return value < AssistSettings::kLowCutOff ? 0.0f
                                                      : std::clamp (static_cast<float> (std::log (std::max (value, 20.0f) / 20.0) / std::log (25.0)), 0.0f, 1.0f);
        if (key == kMudKey || key == kPocketKey) return std::clamp (1.0f + (std::isfinite (value) ? value : 0.0f) / 12.0f, 0.0f, 1.0f);
        return 0.5f;
    }

    bool fromKnob (const std::string& key, float knob, float& value)
    {
        knob = std::clamp (std::isfinite (knob) ? knob : 0.0f, 0.0f, 1.0f);
        if (key == kLowCutKey)
        {
            // The very bottom of the knob: Off; above it 20 Hz .. 500 Hz in octaves.
            value = knob < 0.01f ? 0.0f : std::min (static_cast<float> (20.0 * std::pow (25.0, static_cast<double> (knob))), 500.0f);
            return true;
        }
        if (key == kMudKey || key == kPocketKey)
        {
            value = std::clamp ((knob - 1.0f) * 12.0f, -12.0f, 0.0f);
            return true;
        }
        for (int i = 0; i < count; ++i)
        {
            const auto& s = specs()[static_cast<std::size_t> (i)];
            if (key != s.id) continue;
            if (s.curve == Curve::log && s.min > 0.0f && s.kind == Kind::number)
                value = static_cast<float> (s.min * std::pow (static_cast<double> (s.max) / s.min, static_cast<double> (knob)));
            else if (s.curve == Curve::zeroMid)
                value = fromNormalised (s, knob);
            else
                value = s.min + knob * (s.max - s.min);
            if (s.kind != Kind::number) value = std::round (value);
            else if (s.step > 0.0f) value = s.min + std::round ((value - s.min) / s.step) * s.step;
            value = std::clamp (value, s.min, s.max);
            return true;
        }
        return false;
    }

    int switchOf (const std::string& moduleKey)
    {
        for (int m = 0; m < kModules; ++m)
            if (moduleKey == key (static_cast<Module> (m))) return onParam (static_cast<Module> (m));
        return -1;
    }

    int indexOf (const std::string& key)
    {
        for (int i = 0; i < count; ++i)
            if (key == specs()[static_cast<std::size_t> (i)].id) return i;
        return -1;
    }

    std::string valueText (int index, float value)
    {
        if (index < 0 || index >= count) return {};
        const auto& s = specs()[static_cast<std::size_t> (index)];
        if (s.kind == Kind::toggle) return value >= 0.5f ? "On" : "Off";
        if (s.kind == Kind::choice) return choiceName (s, static_cast<int> (std::lround (value)));
        const std::string unit = s.unit != nullptr ? s.unit : "";
        if (unit == "Hz") return hzText (value);
        if (unit == "dB") return signedDb (value);
        if (unit == "ms") return fixed (value, value < 10.0f ? 1 : 0) + " ms";
        if (unit == "s") return fixed (value, 1) + " s";
        if (unit == "%") return fixed (value, 0) + " %";
        if (unit == ":1") return fixed (value, 1) + ":1";
        if (unit == "st") return (value > 0.0f ? "+" : "") + fixed (value, 0) + " st";
        return fixed (value, 1);
    }

    float valueNorm (int index, float value)
    {
        if (index < 0 || index >= count) return 0.5f;
        const auto& s = specs()[static_cast<std::size_t> (index)];
        if (s.max <= s.min) return 0.5f;
        float n = 0.0f;
        if (s.curve == Curve::log && s.min > 0.0f)
            n = static_cast<float> (std::log (std::max (value, s.min) / s.min) / std::log (s.max / s.min));
        else if (s.curve == Curve::zeroMid)
            n = toNormalised (s, value);
        else
            n = (value - s.min) / (s.max - s.min);
        return std::clamp (std::isfinite (n) ? n : 0.5f, 0.0f, 1.0f);
    }

    std::vector<rack::Module> modules (const State& st, bool full)
    {
        std::vector<rack::Module> out;
        const bool chain = v (st, chainOn) >= 0.5f;

        if (st.link)
        {
            rack::Module m;
            m.key = "mix.assist";
            const auto& a = st.assist;
            m.state = a.neutral() ? rack::State::off : rack::State::on;
            std::string sum;
            auto add = [&] (const std::string& part) { sum += (sum.empty() ? "" : " \xC2\xB7 ") + part; };
            if (a.lowCutOn()) add ("Low cut " + hzText (a.lowCutHz));
            if (a.mudDb < -0.05f) add ("Mud " + signedDb (a.mudDb) + " at " + hzText (a.mudHz));
            if (a.pocketDb < -0.05f) add ("Pocket " + signedDb (a.pocketDb) + " at " + hzText (a.pocketHz));
            m.summary = sum.empty() ? "Off" : sum;
            if (! a.neutral() && st.assistByMixology) m.by = "Mixology's Clean Up / Vocal Pocket";
            if (full)
            {
                m.values.push_back ({ "Low Cut", a.lowCutOn() ? hzText (a.lowCutHz) : "Off", assistNorm (kLowCutKey, a.lowCutOn() ? a.lowCutHz : 0.0f),
                                      false, kLowCutKey });
                m.values.push_back ({ "Mud", a.mudDb < -0.05f ? signedDb (a.mudDb) : "Off", assistNorm (kMudKey, a.mudDb), false, kMudKey });
                m.values.push_back ({ "Pocket", a.pocketDb < -0.05f ? signedDb (a.pocketDb) : "Off", assistNorm (kPocketKey, a.pocketDb), false, kPocketKey });
                if (a.lowCutOn()) m.bands.push_back ({ rack::BandType::lowCut, a.lowCutHz, 0.0f, 0.71f, "Low Cut", true });
                if (a.mudDb < -0.05f) m.bands.push_back ({ rack::BandType::bell, a.mudHz, a.mudDb, static_cast<float> (AssistEq::kMudQ), "Mud", true });
                if (a.pocketDb < -0.05f) m.bands.push_back ({ rack::BandType::bell, a.pocketHz, a.pocketDb, static_cast<float> (AssistEq::kPocketQ), "Pocket", true });
            }
            out.push_back (std::move (m));
        }

        constexpr Module kOrder[] = { Module::gate, Module::eq, Module::comp, Module::multi, Module::trans, Module::tone, Module::loud,
                                      Module::harm, Module::analog, Module::clip, Module::stereo, Module::final, Module::space, Module::limit };
        for (const Module mod : kOrder)
        {
            rack::Module m;
            m.key = key (mod);
            m.switchable = true;   // its own on switch (the Rack also turns the processing on when it is off: step 3)
            const bool on = chain && v (st, onParam (mod)) >= 0.5f;
            m.state = on ? rack::State::on : rack::State::off;
            switch (mod)
            {
                case Module::gate:
                    values (m, st, { { gateThr, "Threshold" }, { gateRange, "Range" }, { gateMode, "Mode" }, { gateAtt, "Attack" },
                                     { gateHold, "Hold" }, { gateRel, "Release" } });
                    gr (m, st, st.meters.gate);
                    break;
                case Module::eq:
                {
                    std::string sum;
                    for (int b = 0; b < kEqBands; ++b)
                    {
                        const int shape = static_cast<int> (std::lround (v (st, eqType (b))));
                        const float hz = v (st, eqFreq (b)), db = v (st, eqGain (b)), q = v (st, eqQ (b));
                        const auto type = bandType (shape);
                        const bool cut = type == rack::BandType::lowCut || type == rack::BandType::highCut;
                        if (! cut && std::abs (db) < 0.05f) continue;   // flat: does nothing
                        const std::string what = cut ? std::string (type == rack::BandType::lowCut ? "Low cut " : "High cut ") + hzText (hz)
                                                     : signedDb (db) + " at " + hzText (hz);
                        if (sum.size() < 60) sum += (sum.empty() ? "" : " \xC2\xB7 ") + what;
                        m.bands.push_back ({ type, hz, cut ? 0.0f : db, q, cut ? (type == rack::BandType::lowCut ? "Low Cut" : "High Cut") : hzText (hz), true });
                        if (cut) m.values.push_back (knob (eqFreq (b), type == rack::BandType::lowCut ? "Low Cut" : "High Cut", hzText (hz), hz, false));
                        else m.values.push_back (knob (eqGain (b), hzText (hz), signedDb (db), db, true));
                    }
                    m.summary = sum.empty() ? "Flat" : sum;
                    {
                        // EQ Dynamic (Beta 22): a knob of its own (the bands' knobs come first, up to six)
                        const float dyn = v (st, eqDyn);
                        if (m.values.size() < static_cast<std::size_t> (rack::kMaxValues))
                            m.values.push_back (knob (eqDyn, "Dynamic", dyn < 0.5f ? std::string ("Off") : std::to_string (static_cast<int> (std::lround (dyn))) + " %", dyn, false));
                        if (dyn >= 0.5f && ! sum.empty()) m.summary = "Dynamic " + std::to_string (static_cast<int> (std::lround (dyn))) + " % \xC2\xB7 " + m.summary;
                    }
                    break;
                }
                case Module::comp:
                    values (m, st, { { compRatio, "Ratio" }, { compThr, "Threshold" }, { compAtt, "Attack" }, { compRel, "Release" },
                                     { compMakeup, "Makeup" }, { compKnee, "Knee" } });
                    gr (m, st, st.meters.comp);
                    break;
                case Module::multi:
                    values (m, st, { { multiLow, "Low" }, { multiMid, "Mid" }, { multiHigh, "High" }, { multiX1, "Low / Mid" },
                                     { multiX2, "Mid / High" }, { multiBands, "Bands" } });
                    gr (m, st, std::max ({ st.meters.multiLow, st.meters.multiMid, st.meters.multiHigh }));
                    break;
                case Module::trans:
                    values (m, st, { { transAttack, "Attack" }, { transSustain, "Sustain" }, { transSpeed, "Speed" }, { transFocus, "Focus" },
                                     { transOut, "Output" }, { transMix, "Mix" } });
                    break;
                case Module::tone:
                    values (m, st, { { tbWarmth, "Warmth" }, { tbPresence, "Presence" }, { tbAir, "Air" }, { tbGlue, "Glue" }, { tbPunch, "Punch" } });
                    break;
                case Module::loud:
                    values (m, st, { { ldDensity, "Density" }, { ldLow, "Low End" }, { ldTop, "Top End" }, { ldTrim, "Trim" } });
                    break;
                case Module::harm:
                    values (m, st, { { harmDrive, "Drive" }, { harmEvenOdd, "Even / Odd" }, { harmTone, "Tone" }, { harmMix, "Mix" } });
                    break;
                case Module::analog:
                    values (m, st, { { anTape, "Tape" }, { anConsole, "Console" }, { anNoise, "Noise" }, { anMix, "Mix" } });
                    break;
                case Module::clip:
                    values (m, st, { { clipThr, "Threshold" }, { clipSoft, "Softness" }, { clipOs, "Oversampling" }, { clipMix, "Mix" } });
                    gr (m, st, st.meters.clip);
                    break;
                case Module::stereo:
                    values (m, st, { { stWidth, "Width" }, { stLow, "Low Width" }, { stMid, "Mid Width" }, { stHigh, "High Width" },
                                     { stLowX, "Low Below" }, { stHighX, "High Above" } });
                    break;
                case Module::final:
                    values (m, st, { { finOut, "Output" }, { finWidth, "Width" }, { finAir, "Air" }, { finGlue, "Glue" } });
                    break;
                case Module::space:
                {
                    const std::uint32_t bits = spaceLayers (st.values);
                    m.layers = on ? bits : 0u;   // the same bit order as rack::SpaceLayer
                    static const int kMix[kSpaceTypes] = { revMix, dlyMix, choMix, phaMix, hallMix, roomMix, spxMix };
                    for (int t = 0; t < kSpaceTypes; ++t)
                        if ((bits & (1u << t)) != 0)
                        {
                            const std::string name = effectNames (1u << t) + " Mix";
                            m.values.push_back (knob (kMix[t], name, valueText (kMix[t], v (st, kMix[t])), v (st, kMix[t]), false));
                        }
                    values (m, st, { { duckAmount, "Ducking" }, { spLowCut, "Low Cut" }, { spHighCut, "High Cut" } });
                    m.summary = effectNames (bits);
                    if (! st.duckNames.empty() && v (st, duckAmount) > 0.05f) m.summary += " \xC2\xB7 ducks under " + st.duckNames;
                    m.meterKind = rack::MeterKind::level;
                    m.meter = st.hasMeters ? st.meters.spaceWet : -100.0f;
                    break;
                }
                case Module::limit:
                    values (m, st, { { limCeil, "Ceiling" }, { limIn, "Input" }, { limRel, "Release" }, { limLook, "Lookahead" },
                                     { limChar, "Character" }, { limDetect, "Detect" } });
                    gr (m, st, st.meters.limit);
                    break;
                case Module::count:
                    break;
            }
            if (m.summary.empty()) m.summary = summaryOf (m);
            if (! on) m.summary = "Off";
            if (! full)
            {
                m.values.clear();
                m.bands.clear();
            }
            out.push_back (std::move (m));
        }
        return out;
    }
}
