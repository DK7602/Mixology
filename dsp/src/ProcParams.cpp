#include "mixology/ProcParams.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mixology::proc
{
    namespace
    {
        constexpr ParamSpec num (const char* id, const char* name, float lo, float hi, float def, Curve c, float step, const char* unit)
        {
            return { id, name, Kind::number, lo, hi, def, c, step, unit, "" };
        }
        constexpr ParamSpec toggle (const char* id, const char* name, bool def = false)
        {
            return { id, name, Kind::toggle, 0.0f, 1.0f, def ? 1.0f : 0.0f, Curve::linear, 1.0f, "", "Off|On" };
        }
        constexpr ParamSpec choice (const char* id, const char* name, const char* choices, int options, int def)
        {
            return { id, name, Kind::choice, 0.0f, static_cast<float> (options - 1), static_cast<float> (def), Curve::linear, 1.0f, "", choices };
        }

        constexpr auto L = Curve::linear;
        constexpr auto G = Curve::log;
        constexpr const char* kShapes = "Bell|Low shelf|High shelf|Low cut|High cut";

        constexpr ParamSpec band (int b, int field, int shape, float hz, float q)
        {
            constexpr const char* typeIds[] = { "eq1_type", "eq2_type", "eq3_type", "eq4_type", "eq5_type", "eq6_type" };
            constexpr const char* freqIds[] = { "eq1_freq", "eq2_freq", "eq3_freq", "eq4_freq", "eq5_freq", "eq6_freq" };
            constexpr const char* gainIds[] = { "eq1_gain", "eq2_gain", "eq3_gain", "eq4_gain", "eq5_gain", "eq6_gain" };
            constexpr const char* qIds[] = { "eq1_q", "eq2_q", "eq3_q", "eq4_q", "eq5_q", "eq6_q" };
            constexpr const char* typeNames[] = { "EQ 1 Shape", "EQ 2 Shape", "EQ 3 Shape", "EQ 4 Shape", "EQ 5 Shape", "EQ 6 Shape" };
            constexpr const char* freqNames[] = { "EQ 1 Freq", "EQ 2 Freq", "EQ 3 Freq", "EQ 4 Freq", "EQ 5 Freq", "EQ 6 Freq" };
            constexpr const char* gainNames[] = { "EQ 1 Gain", "EQ 2 Gain", "EQ 3 Gain", "EQ 4 Gain", "EQ 5 Gain", "EQ 6 Gain" };
            constexpr const char* qNames[] = { "EQ 1 Q", "EQ 2 Q", "EQ 3 Q", "EQ 4 Q", "EQ 5 Q", "EQ 6 Q" };
            switch (field)
            {
                case 0: return choice (typeIds[b], typeNames[b], kShapes, 5, shape);
                case 1: return num (freqIds[b], freqNames[b], 20.0f, 20000.0f, hz, G, 0.0f, "Hz");
                case 2: return num (gainIds[b], gainNames[b], -12.0f, 12.0f, 0.0f, L, 0.1f, "dB");
                default: return num (qIds[b], qNames[b], 0.3f, 10.0f, q, G, 0.0f, "");
            }
        }

        // clang-format off
        constexpr std::array<ParamSpec, count> kSpecs {{
            toggle ("chain_on", "Processing", true),

            toggle ("eq_on", "EQ"),
            choice ("eq_mode", "EQ Character", "Precision|Smooth|Natural", 3, 0),
            band (0, 0, 1, 80.0f, 0.7f),    band (0, 1, 1, 80.0f, 0.7f),    band (0, 2, 1, 80.0f, 0.7f),    band (0, 3, 1, 80.0f, 0.7f),
            band (1, 0, 0, 250.0f, 1.0f),   band (1, 1, 0, 250.0f, 1.0f),   band (1, 2, 0, 250.0f, 1.0f),   band (1, 3, 0, 250.0f, 1.0f),
            band (2, 0, 0, 800.0f, 0.7f),   band (2, 1, 0, 800.0f, 0.7f),   band (2, 2, 0, 800.0f, 0.7f),   band (2, 3, 0, 800.0f, 0.7f),
            band (3, 0, 0, 2500.0f, 1.0f),  band (3, 1, 0, 2500.0f, 1.0f),  band (3, 2, 0, 2500.0f, 1.0f),  band (3, 3, 0, 2500.0f, 1.0f),
            band (4, 0, 0, 7000.0f, 0.9f),  band (4, 1, 0, 7000.0f, 0.9f),  band (4, 2, 0, 7000.0f, 0.9f),  band (4, 3, 0, 7000.0f, 0.9f),
            band (5, 0, 2, 12000.0f, 0.7f), band (5, 1, 2, 12000.0f, 0.7f), band (5, 2, 2, 12000.0f, 0.7f), band (5, 3, 2, 12000.0f, 0.7f),

            toggle ("comp_on", "Compressor"),
            num ("comp_thr", "Comp Threshold", -60.0f, 0.0f, -18.0f, L, 0.1f, "dB"),
            num ("comp_ratio", "Comp Ratio", 1.0f, 20.0f, 3.0f, G, 0.0f, ":1"),
            num ("comp_att", "Comp Attack", 0.1f, 100.0f, 10.0f, G, 0.0f, "ms"),
            num ("comp_rel", "Comp Release", 10.0f, 1000.0f, 120.0f, G, 0.0f, "ms"),
            num ("comp_makeup", "Comp Makeup", 0.0f, 24.0f, 0.0f, L, 0.1f, "dB"),
            choice ("comp_knee", "Comp Knee", "Soft|Medium|Hard", 3, 0),

            toggle ("multi_on", "Multiband"),
            num ("multi_x1", "Multiband Low/Mid", 40.0f, 1000.0f, 160.0f, G, 0.0f, "Hz"),
            num ("multi_x2", "Multiband Mid/High", 800.0f, 12000.0f, 3500.0f, G, 0.0f, "Hz"),
            num ("multi_low", "Multiband Low", 0.0f, 100.0f, 30.0f, L, 1.0f, "%"),
            num ("multi_mid", "Multiband Mid", 0.0f, 100.0f, 20.0f, L, 1.0f, "%"),
            num ("multi_high", "Multiband High", 0.0f, 100.0f, 20.0f, L, 1.0f, "%"),
            choice ("multi_bands", "Multiband Bands", "3 bands|2 bands", 2, 0),

            toggle ("trans_on", "Transient"),
            num ("trans_attack", "Transient Attack", -100.0f, 100.0f, 0.0f, L, 1.0f, "%"),
            num ("trans_sustain", "Transient Sustain", -100.0f, 100.0f, 0.0f, L, 1.0f, "%"),
            num ("trans_speed", "Transient Speed", 1.0f, 50.0f, 10.0f, G, 0.0f, "ms"),
            num ("trans_out", "Transient Output", -12.0f, 12.0f, 0.0f, L, 0.1f, "dB"),
            num ("trans_mix", "Transient Mix", 0.0f, 100.0f, 100.0f, L, 1.0f, "%"),
            choice ("trans_focus", "Transient Focus", "Full|Lows|Highs", 3, 0),

            toggle ("gate_on", "Gate"),
            num ("gate_thr", "Gate Threshold", -80.0f, 0.0f, -50.0f, L, 0.1f, "dB"),
            num ("gate_range", "Gate Range", 0.0f, 80.0f, 18.0f, L, 0.1f, "dB"),
            num ("gate_att", "Gate Attack", 0.1f, 50.0f, 1.0f, G, 0.0f, "ms"),
            num ("gate_hold", "Gate Hold", 0.0f, 500.0f, 40.0f, L, 1.0f, "ms"),
            num ("gate_rel", "Gate Release", 10.0f, 2000.0f, 150.0f, G, 0.0f, "ms"),
            choice ("gate_mode", "Gate Mode", "Gate|Expander", 2, 0),

            toggle ("lim_on", "Limiter"),
            num ("lim_in", "Limiter Input", 0.0f, 12.0f, 0.0f, L, 0.1f, "dB"),
            num ("lim_ceil", "Limiter Ceiling", -12.0f, 0.0f, -1.0f, L, 0.1f, "dB"),
            num ("lim_rel", "Limiter Release", 5.0f, 1000.0f, 80.0f, G, 0.0f, "ms"),
            num ("lim_look", "Limiter Lookahead", 0.0f, 5.0f, 1.5f, L, 0.1f, "ms"),
            num ("lim_char", "Limiter Character", 0.0f, 100.0f, 30.0f, L, 1.0f, "%"),
            choice ("lim_detect", "Limiter Detect", "Sample peak|True peak", 2, 1),

            toggle ("tb_on", "Tone Balance"),
            num ("tb_warmth", "Warmth", -6.0f, 6.0f, 0.0f, L, 0.1f, "dB"),
            num ("tb_presence", "Presence", -6.0f, 6.0f, 0.0f, L, 0.1f, "dB"),
            num ("tb_air", "Air", -6.0f, 6.0f, 0.0f, L, 0.1f, "dB"),
            num ("tb_glue", "Glue", 0.0f, 100.0f, 0.0f, L, 1.0f, "%"),
            num ("tb_punch", "Punch", -100.0f, 100.0f, 0.0f, L, 1.0f, "%"),

            toggle ("ld_on", "Loudness"),
            num ("ld_trim", "Loudness Trim", -12.0f, 12.0f, 0.0f, L, 0.1f, "dB"),
            num ("ld_density", "Density", 0.0f, 100.0f, 0.0f, L, 1.0f, "%"),
            num ("ld_low", "Low End", -6.0f, 6.0f, 0.0f, L, 0.1f, "dB"),
            num ("ld_top", "Top End", -6.0f, 6.0f, 0.0f, L, 0.1f, "dB"),

            toggle ("clip_on", "Clip Control"),
            num ("clip_thr", "Clip Threshold", -12.0f, 0.0f, -2.0f, L, 0.1f, "dB"),
            num ("clip_soft", "Clip Softness", 0.0f, 100.0f, 50.0f, L, 1.0f, "%"),
            choice ("clip_os", "Clip Oversampling", "1x|2x|4x|8x", 4, 2),
            num ("clip_mix", "Clip Mix", 0.0f, 100.0f, 100.0f, L, 1.0f, "%"),

            toggle ("harm_on", "Harmonics"),
            num ("harm_drive", "Harmonic Drive", 0.0f, 100.0f, 25.0f, L, 1.0f, "%"),
            num ("harm_evenodd", "Even/Odd", 0.0f, 100.0f, 60.0f, L, 1.0f, "%"),
            num ("harm_tone", "Harmonic Tone", -6.0f, 6.0f, 0.0f, L, 0.1f, "dB"),
            num ("harm_mix", "Harmonic Mix", 0.0f, 100.0f, 50.0f, L, 1.0f, "%"),

            toggle ("an_on", "Analog Touch"),
            num ("an_tape", "Tape", 0.0f, 100.0f, 30.0f, L, 1.0f, "%"),
            num ("an_console", "Console", 0.0f, 100.0f, 20.0f, L, 1.0f, "%"),
            num ("an_noise", "Noise", 0.0f, 100.0f, 0.0f, L, 1.0f, "%"),
            num ("an_mix", "Analog Mix", 0.0f, 100.0f, 100.0f, L, 1.0f, "%"),

            toggle ("fin_on", "Final Touch"),
            num ("fin_width", "Final Width", 0.0f, 200.0f, 100.0f, L, 1.0f, "%"),
            num ("fin_air", "Final Air", -6.0f, 6.0f, 0.0f, L, 0.1f, "dB"),
            num ("fin_glue", "Final Glue", 0.0f, 100.0f, 0.0f, L, 1.0f, "%"),
            num ("fin_out", "Final Output", -12.0f, 12.0f, 0.0f, L, 0.1f, "dB"),

            toggle ("st_on", "Stereo"),
            num ("st_width", "Width", 0.0f, 200.0f, 100.0f, L, 1.0f, "%"),
            num ("st_low", "Low Width", 0.0f, 200.0f, 100.0f, L, 1.0f, "%"),
            num ("st_mid", "Mid Width", 0.0f, 200.0f, 100.0f, L, 1.0f, "%"),
            num ("st_high", "High Width", 0.0f, 200.0f, 100.0f, L, 1.0f, "%"),
            num ("st_lowx", "Low Width Below", 60.0f, 500.0f, 150.0f, G, 0.0f, "Hz"),
            num ("st_highx", "High Width Above", 1500.0f, 10000.0f, 4000.0f, G, 0.0f, "Hz"),
        }};
        // clang-format on

        constexpr int kOnParams[] = { gateOn, eqOn, compOn, multiOn, transOn, tbOn, ldOn, harmOn, anOn, clipOn, stOn, finOn, limOn };
        constexpr const char* kModuleKeys[] = { "gate", "eq", "comp", "multi", "trans", "balance", "loud", "harm", "analog", "clip", "width", "final", "limit" };
        static_assert (std::size (kOnParams) == kModules);
        static_assert (std::size (kModuleKeys) == kModules);
    }

    const std::array<ParamSpec, count>& specs() { return kSpecs; }

    int onParam (Module m) { return kOnParams[static_cast<int> (m)]; }
    const char* moduleKey (Module m) { return kModuleKeys[static_cast<int> (m)]; }

    int indexOf (std::string_view id)
    {
        for (int i = 0; i < count; ++i)
            if (id == kSpecs[static_cast<std::size_t> (i)].id) return i;
        return -1;
    }

    Values defaults()
    {
        Values v {};
        for (int i = 0; i < count; ++i) v[static_cast<std::size_t> (i)] = kSpecs[static_cast<std::size_t> (i)].def;
        return v;
    }

    float sanitise (int index, float value)
    {
        if (index < 0 || index >= count) return 0.0f;
        const auto& s = kSpecs[static_cast<std::size_t> (index)];
        if (! std::isfinite (value)) return s.def;
        value = std::clamp (value, s.min, s.max);
        if (s.kind != Kind::number) return std::round (value);
        if (s.step > 0.0f) value = std::clamp (s.min + std::round ((value - s.min) / s.step) * s.step, s.min, s.max);
        return value;
    }

    bool isOn (const Values& v, Module m)
    {
        return v[chainOn] > 0.5f && v[static_cast<std::size_t> (onParam (m))] > 0.5f;
    }

    void meterValues (const Meters& m, float* out)
    {
        const float v[kMeterValues] = { m.inL, m.inR, m.outL, m.outR, m.comp, m.multiLow, m.multiMid, m.multiHigh, m.trans, m.gate, m.limit, m.clip, m.glue };
        std::copy (v, v + kMeterValues, out);
    }

    Meters metersFrom (const float* v, std::uint32_t running)
    {
        Meters m;
        m.inL = v[0]; m.inR = v[1]; m.outL = v[2]; m.outR = v[3];
        m.comp = v[4]; m.multiLow = v[5]; m.multiMid = v[6]; m.multiHigh = v[7];
        m.trans = v[8]; m.gate = v[9]; m.limit = v[10]; m.clip = v[11]; m.glue = v[12];
        m.running = running;
        return m;
    }
}
