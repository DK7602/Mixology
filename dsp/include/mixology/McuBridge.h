#pragma once

#include "mixology/BridgeProtocol.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

/** Mixology's bridge for DAWs that take a Mackie Control surface (Logic Pro, Cakewalk / Sonar).

    No script runs in the DAW: Mixology itself is the surface. This translates between the bridge protocol
    the session speaks (BridgeProtocol.h, the same messages the Cubase script and the FL Studio / Live scripts
    send) and the Mackie Control protocol on the two virtual MIDI ports, so BridgeSession can't tell the DAWs
    apart:
        session --frames--> fromMixology()   ...  takeForDaw() --Mackie Control--> the DAW
        session <--frames-- takeForMixology() ... fromDaw()   <--Mackie Control-- the DAW
    The strips are the surface's 8 channels; the DAW decides what they show (its bank). What the surface
    knows of a channel is what the DAW sends it: a short name on the display (6 letters), the fader position,
    the mute / solo lights, the pan knob's ring and the text under the name. So:
    - Names are expanded back to the full track names Mixology knows (its Links), when the short name fits
      exactly one of them ("LdVcl" is "Lead Vocal"); the hash Mixology checks is the expanded name's.
    - The fader's dB comes from the DAW's fader curve, built in per DAW (anchored at the 0 dB position each
      DAW is known to use) and corrected by every dB value the DAW's display shows for a still fader.
    - After a bank move nothing can be moved until the DAW has shown the new names (a move must never land
      on the channel that slid into the strip). Text the DAW shows for a moment where a name goes (a knob's
      value, Logic's "Volume") is not taken as a name.
    What each DAW does differently (from Cakewalk's published surface source and captures of Logic):
    - Logic asks "who are you?" and expects the Mackie handshake; it ignores buttons for about 1.5 s after.
      0 dB is fader 12443 of 16383. The text under the names is the pan (-64 .. +63).
    - Cakewalk connects without a handshake (its default), and resends everything after a "wake-up". Its
      faders have 10 bits; 0 dB is 101/127. The knob accelerates (several steps in one message move more),
      and shows its value in the name's place for about 1.5 s. Under the names it shows labels until
      NAME/VALUE is pressed.
    Framework-free and clock-injected, like BridgeSession; message thread only. */
namespace mixology::mcu
{
    enum class Dialect { logic, cakewalk };

    constexpr int kStrips = 8;
    constexpr int kFaderMax = 16383;   // pitch bend, 14 bits

    // Mackie Control notes (Note On, channel 1; velocity 0x7F press / light on, 0 release / off, 1 blink)
    constexpr std::uint8_t kNoteRec = 0x00, kNoteSolo = 0x08, kNoteMute = 0x10, kNoteSelect = 0x18;
    constexpr std::uint8_t kNoteBankLeft = 0x2E, kNoteBankRight = 0x2F, kNoteChannelLeft = 0x30, kNoteChannelRight = 0x31;
    constexpr std::uint8_t kNoteNameValue = 0x34;
    constexpr std::uint8_t kNoteTouch = 0x68;
    constexpr std::uint8_t kCcVPot = 0x10, kCcRing = 0x30;

    /** Fader position on the surface (pitch bend) <-> the 0..1 Mixology works with, per DAW. */
    double faderToNormalised (Dialect, int pitchBend);
    int normalisedToFader (Dialect, double normalised);

    /** A DAW's fader curve on the surface: position (0..1) <-> dB. Built in per DAW, and every dB value the
        DAW's display shows with a known fader position corrects it (it can then be saved: serialise). */
    class McuLaw
    {
    public:
        explicit McuLaw (Dialect);
        double toDb (double normalised) const;        // FaderLaw::kMinusInf for -inf
        double toNormalised (double db) const;
        void learn (double normalised, double db);
        std::size_t learned() const { return points.size(); }
        std::string serialise() const;
        void deserialise (const std::string&);
        static double builtInDb (Dialect, double normalised);
        static double builtInNormalised (Dialect, double db);
    private:
        Dialect dialect;
        std::map<int, double> points;   // position (14-bit grid) -> dB, as the DAW's display showed it
    };

    /** The pan a DAW shows, -1 (left) .. 1 (right); false when the text isn't a pan. */
    bool parsePanText (Dialect, const std::string& text, double& pan);
    /** A dB value a DAW shows on the display ("-6.0", "-6.0dB", "+2.4", "-inf"): false for anything else, pans
        included (they have no decimal point). */
    bool parseDisplayDb (const std::string& text, double& db);

    /** Fits a name from the display back to the full track names Mixology knows. Returns the full name when
        exactly one fits (its letters and digits, in order, starting with the same one), else the short name. */
    std::string expandName (const std::string& shortName, const std::vector<std::string>& fullNames);
    /** The same for a whole bank: a track one strip is sure of is no other strip's, and a track seen with one
        short name (`seenAs`, kept between banks) can't show another. What fits several tracks stays short:
        guessing could move the wrong fader. */
    std::vector<std::string> expandNames (const std::vector<std::string>& shown, const std::vector<std::string>& fullNames,
                                          std::map<std::string, std::string>& seenAs);

    class McuBridge
    {
    public:
        explicit McuBridge (Dialect);

        Dialect dialect() const { return kind; }
        const char* dawName() const;

        /** A protocol frame from BridgeSession (F0 7D 4D 58 ... F7). */
        void fromMixology (const std::vector<std::uint8_t>& frame, double now);
        /** One complete MIDI message from the DAW (a SysEx frame F0..F7, or a 2 / 3 byte channel message). */
        void fromDaw (const std::uint8_t* data, std::size_t size, double now);
        void tick (double now);

        std::vector<std::vector<std::uint8_t>> takeForMixology();
        std::vector<std::vector<std::uint8_t>> takeForDaw();

        /** The full track names Mixology knows (its Links), to expand the display's short names. */
        void setTrackNames (std::vector<std::string>);

        /** The ports (re)opened: forget the DAW's state, and ask it to send everything again. */
        void reset (double now);

        bool dawSeen() const { return seen; }
        McuLaw& law() { return faderLaw; }
        const McuLaw& law() const { return faderLaw; }

        // ---- tuning ----
        static constexpr double kSettle = 0.12;        // the names are read once that row didn't change for this long
        static constexpr double kBankWait = 0.35;      // a bank press the DAW didn't answer (already at the end)
        static constexpr double kTouchHold = 0.25;     // the fader is released this long after the last move
        static constexpr double kReadWait = 0.3;       // after a move, the display's dB is awaited this long
        static constexpr int kMovesBeforeNoWait = 3;   // ... until this many moves showed none
        static constexpr double kNameHold = 2.0;       // after Mixology touched a strip, other text in its name's place is passing
        static constexpr double kPanStepWait = 0.3;    // a pan readback: next step after this, or when the text came
        static constexpr int kMaxPanSteps = 8;
        static constexpr double kWakeInterval = 2.0;   // "I'm here" to the DAW while it hasn't spoken
        static constexpr double kLogicButtonPause = 1.6; // Logic ignores buttons this long after the handshake

        /** For tests and the log: what each strip shows now. */
        struct StripView
        {
            std::string name;          // the stable name from the display (trimmed)
            std::string title;         // as Mixology gets it (expanded)
            int fader = 0;
            bool mute = false, solo = false;
            double pan = 0.0;
            bool hasPan = false;
            std::string valueText;     // the display's lower row for this strip (trimmed)
        };
        StripView view (int strip) const;

    private:
        struct Strip
        {
            int fader = 0;
            bool hasFader = false;
            double faderChangedAt = -1.0;
            double volumeHoldUntil = -1.0;    // after Mixology moved it: report once the display read it, or then
            bool holdPending = false;
            bool hasDisplayDb = false;        // the display's dB for this position was learned
            bool mute = false, solo = false;
            int ring = -1;                    // the knob's ring position 1..11 (0 = off), -1 = not sent
            double pan = 0.0;
            bool hasPan = false;
            bool panFromText = false;         // the pan came from the display's text (else from the ring: coarse)
            std::string valueText;            // the display's lower row for this strip
            double valueChangedAt = -1.0;
            std::string name;                 // the stable name (text shown for a moment in its place is ignored)
            double nameHoldUntil = -1.0;
            double topChangedAt = -1.0;       // this strip's part of the names' row last changed then
            double touchUntil = -1.0;         // touched (by Mixology) until then; -1 = released
            // a pan move in progress (the knob turns in steps; the display says where it got)
            bool panMoving = false;
            double panTarget = 0.0;
            int panSteps = 0;
            double panSentAt = 0.0;
            double panReadAt = -1.0;

            // what Mixology was last told
            std::string sentTitle;
            bool titleSent = false;
            std::uint32_t hash = 0;
            std::string sentVolume, sentPan;
            double sentVolumeValue = -1.0, sentPanValue = -1.0;
            int sentMute = -1, sentSolo = -1;
        };

        struct Later { double at = 0.0; std::vector<std::uint8_t> bytes; };

        Dialect kind;
        McuLaw faderLaw;
        std::array<Strip, kStrips> strips {};
        std::array<char, 112> lcd {};      // the two display rows, 56 characters each
        bool seen = false;
        bool helloWaiting = false;         // Mixology said hello before the DAW spoke
        std::uint16_t helloToken = 0;
        bool displayShowsDb = false;       // this DAW has shown a touched fader's dB (Mixology then waits for it)
        int movesWithoutDb = 0;
        double lastWake = -1.0e9;
        double buttonsFrom = -1.0e9;       // Logic: buttons pressed before this are ignored, so they wait
        std::vector<Later> later;
        int valueToggles = 0;              // Cakewalk: NAME/VALUE presses to show values under the names
        double valueCheckAt = -1.0;
        bool waitingForBank = false;
        double bankSentAt = 0.0;
        bool bankAnswered = false;         // the names changed since the bank press
        std::vector<std::string> names;
        std::map<std::string, std::string> seenAs;   // full name -> the short name the display showed for it
        int errors = 0;

        std::vector<std::vector<std::uint8_t>> toMixology, toDaw;

        void send (bridge::Cmd, const bridge::Message&);
        void announce (std::uint16_t token);
        void sendAck (std::uint16_t token, bridge::AckStatus);
        void log (const std::string&);
        void note (std::uint8_t note, std::uint8_t velocity);
        void press (std::uint8_t note, double now);
        void handle (const bridge::Message&, double now);
        Strip* target (const bridge::Message&, double now);
        bool settled (int strip, double now) const;
        void flush (bool everything, double now);
        void dump (double now);
        void onSysex (const std::uint8_t* data, std::size_t size, double now);
        void markSeen (double now);
        std::string segment (int strip, int row) const;
        bool isPassingText (const std::string& text) const;
        std::array<std::string, kStrips> titles();
        void readDisplay (double now);
        void stepPan (int strip, double now);
        void sendFader (int strip, double normalised, double now);
        void wake (double now);
        void sysexReply (std::uint8_t cmd, const std::vector<std::uint8_t>& payload);
    };
}
