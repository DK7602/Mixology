#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

/** A small binary packer for what Mixology keeps with the project (the History, the advice's moments, the restore
    points). Little-endian whatever the machine, so a project moves between Windows and Mac. The Reader never reads
    past the end: a short or damaged block reads as zeros and ok() turns false. Framework-free. */
namespace mixology::pack
{
    class Writer
    {
    public:
        void u8 (std::uint8_t v) { out.push_back (static_cast<char> (v)); }
        void u16 (std::uint16_t v) { for (int i = 0; i < 2; ++i) u8 (static_cast<std::uint8_t> (v >> (8 * i))); }
        void u32 (std::uint32_t v) { for (int i = 0; i < 4; ++i) u8 (static_cast<std::uint8_t> (v >> (8 * i))); }
        void u64 (std::uint64_t v) { for (int i = 0; i < 8; ++i) u8 (static_cast<std::uint8_t> (v >> (8 * i))); }
        void i16 (std::int16_t v) { u16 (static_cast<std::uint16_t> (v)); }
        void i32 (std::int32_t v) { u32 (static_cast<std::uint32_t> (v)); }
        void f32 (float v) { std::uint32_t b; std::memcpy (&b, &v, sizeof b); u32 (b); }
        void f64 (double v) { std::uint64_t b; std::memcpy (&b, &v, sizeof b); u64 (b); }
        void flag (bool v) { u8 (v ? 1 : 0); }
        void str (const std::string& s) { u32 (static_cast<std::uint32_t> (s.size())); out += s; }
        /** A level in 1/100 dB (or any value in hundredths) as 16 bits: -327.68 .. +327.67. */
        void centi (double v) { i16 (static_cast<std::int16_t> (std::lround (clamp (v * 100.0, -32768.0, 32767.0)))); }
        /** A level in 1/10 dB as 16 bits: -3276.8 .. +3276.7. */
        void deci (double v) { i16 (static_cast<std::int16_t> (std::lround (clamp (v * 10.0, -32768.0, 32767.0)))); }
        /** A share 0..1 as 16 bits. */
        void unit (double v) { u16 (static_cast<std::uint16_t> (std::lround (clamp (v, 0.0, 1.0) * 65535.0))); }
        /** A share 0..1 as 8 bits. */
        void unit8 (double v) { u8 (static_cast<std::uint8_t> (std::lround (clamp (v, 0.0, 1.0) * 255.0))); }
        /** -1..1 as 16 bits. */
        void signedUnit (double v) { i16 (static_cast<std::int16_t> (std::lround (clamp (v, -1.0, 1.0) * 32767.0))); }

        const std::string& bytes() const { return out; }
        std::string take() { return std::move (out); }

    private:
        std::string out;
        static double clamp (double v, double lo, double hi) { return std::isfinite (v) ? (v < lo ? lo : v > hi ? hi : v) : 0.0; }
    };

    class Reader
    {
    public:
        explicit Reader (const std::string& b) : data (b) {}

        std::uint8_t u8()
        {
            if (at >= data.size()) { good = false; return 0; }
            return static_cast<std::uint8_t> (data[at++]);
        }
        std::uint16_t u16() { std::uint16_t v = 0; for (int i = 0; i < 2; ++i) v |= static_cast<std::uint16_t> (u8() << (8 * i)); return v; }
        std::uint32_t u32() { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t> (u8()) << (8 * i); return v; }
        std::uint64_t u64() { std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t> (u8()) << (8 * i); return v; }
        std::int16_t i16() { return static_cast<std::int16_t> (u16()); }
        std::int32_t i32() { return static_cast<std::int32_t> (u32()); }
        float f32() { const auto b = u32(); float v; std::memcpy (&v, &b, sizeof v); return std::isfinite (v) ? v : 0.0f; }
        double f64() { const auto b = u64(); double v; std::memcpy (&v, &b, sizeof v); return std::isfinite (v) ? v : 0.0; }
        bool flag() { return u8() != 0; }
        std::string str()
        {
            const auto n = u32();
            if (n > data.size() - std::min (at, data.size())) { good = false; at = data.size(); return {}; }
            std::string s = data.substr (at, n);
            at += n;
            return s;
        }
        double centi() { return i16() / 100.0; }
        double deci() { return i16() / 10.0; }
        double unit() { return u16() / 65535.0; }
        double unit8() { return u8() / 255.0; }
        double signedUnit() { return i16() / 32767.0; }

        /** A count read from the block, when each item takes at least `minBytes`: more than what's left = damaged. */
        std::uint32_t count (std::size_t minBytes = 1)
        {
            const auto n = u32();
            if (minBytes > 0 && n > (data.size() - std::min (at, data.size())) / minBytes) { good = false; return 0; }
            return n;
        }

        bool ok() const { return good; }
        bool atEnd() const { return at >= data.size(); }

    private:
        const std::string& data;
        std::size_t at = 0;
        bool good = true;
    };
}
