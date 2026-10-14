#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/** Mixology <-> Cubase bridge messages.

    Every message is one SysEx frame sent over a virtual MIDI port:
        F0 7D 4D 58 <version> <command> <payload...> F7
    0x7D is the MIDI "non-commercial / private use" manufacturer ID; 4D 58 = "MX".
    All payload bytes are 7-bit. Numbers are packed big-endian in 7-bit groups:
        value21  normalised 0..1 as 21 bits (3 bytes), resolution ~5e-7
        token14  request id (2 bytes) so replies can be matched to requests
        hash28   FNV-1a hash of the channel name's UTF-8 bytes, low 28 bits (4 bytes)
        id28     FNV-1a hash of Cubase's own channel ID (getUniqueIDString / getRuntimeID), 0 = not available
        text     length byte (<= 127) then two bytes per UTF-8 byte (high nibble, low nibble)

    The Cubase side is bridge/cubase/Mixology/Mixology_Bridge.js; both sides must agree on this layout.
    Bump kProtocolVersion on any change. */
namespace mixology::bridge
{
    constexpr std::uint8_t kSysexStart = 0xF0;
    constexpr std::uint8_t kSysexEnd = 0xF7;
    constexpr std::uint8_t kManufacturer = 0x7D;
    constexpr std::uint8_t kTag1 = 0x4D;   // 'M'
    constexpr std::uint8_t kTag2 = 0x58;   // 'X'
    constexpr std::uint8_t kProtocolVersion = 1;
    constexpr int kLatestScriptVersion = 4;   // bridge/cubase/.../Mixology_Bridge.js SCRIPT_VERSION (v4: pan from Cubase only)

    constexpr std::uint32_t kValueMax = (1u << 21) - 1u;
    constexpr std::size_t kMaxTextBytes = 63;

    enum class Cmd : std::uint8_t
    {
        // Mixology -> Cubase
        hello        = 0x01,   // token: session nonce
        requestDump  = 0x02,   // ask for every slot's state
        setVolume    = 0x10,   // slot, token, hash, id, value
        setPan       = 0x11,   // slot, token, hash, id, value
        setMute      = 0x12,   // slot, token, hash, id, flag
        setSolo      = 0x13,   // slot, token, hash, id, flag
        bank         = 0x20,   // flag = BankAction
        ping         = 0x30,   // token

        // Cubase -> Mixology
        helloAck     = 0x41,   // flag = script version, slot = slot count, token = nonce echoed
        slotTitle    = 0x42,   // slot, hash, id, text (empty text = no channel in this slot)
        slotVolume   = 0x43,   // slot, value, text (Cubase's own display text, e.g. "-6.02")
        slotPan      = 0x44,   // slot, value, text
        slotMute     = 0x45,   // slot, flag
        slotSolo     = 0x46,   // slot, flag
        slotColour   = 0x47,   // slot, rgb (0xRRGGBB), flag = 1 when Cubase reports a colour
        slotAutomation = 0x48, // slot, flag bit 0 = read on, bit 1 = write on
        ack          = 0x50,   // token, flag = AckStatus
        log          = 0x51,   // text
        pong         = 0x52,   // token
    };

    enum class BankAction : std::uint8_t { reset = 0, previous = 1, next = 2, shiftLeft = 3, shiftRight = 4 };

    enum class AckStatus : std::uint8_t
    {
        applied = 0,
        nameMismatch = 1,     // the slot now holds a different channel: nothing was changed
        emptySlot = 2,        // no channel in that slot
        badMessage = 3,
        automationWrite = 4,  // the channel is writing automation: refused so no automation gets written
    };

    struct Message
    {
        Cmd cmd = Cmd::ping;
        int slot = 0;
        std::uint16_t token = 0;
        std::uint32_t hash = 0;
        std::uint32_t channelId = 0;
        double value = 0.0;
        int flag = 0;
        std::uint32_t rgb = 0;
        std::string text;
    };

    /** FNV-1a over the UTF-8 bytes, low 28 bits (fits in four 7-bit bytes). */
    std::uint32_t nameHash (const std::string& utf8);

    /** Truncates to at most maxBytes without cutting a UTF-8 sequence in half. */
    std::string truncateUtf8 (const std::string& utf8, std::size_t maxBytes);

    std::vector<std::uint8_t> encode (const Message&);

    /** Returns nothing for frames that aren't ours, are malformed, or use another protocol version. */
    std::optional<Message> decode (const std::uint8_t* data, std::size_t size);
    inline std::optional<Message> decode (const std::vector<std::uint8_t>& bytes) { return decode (bytes.data(), bytes.size()); }

    const char* commandName (Cmd);
    const char* ackStatusText (AckStatus);
}
