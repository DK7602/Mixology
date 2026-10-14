#pragma once

#include "mixology/BridgeProtocol.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/** A stand-in for Cubase + the Mixology Bridge script, for tests: a project with channels, a bank of
    slots looking at them, a non-linear fader law with Cubase-style display text, and the same
    accept / refuse rules as bridge/cubase/Mixology/Mixology_Bridge.js. */
struct FakeCubase
{
    struct Channel
    {
        std::string name;
        double volume = 0.7766;   // normalised
        double pan = 0.5;
        bool mute = false, solo = false, read = false, write = false;
        std::uint32_t rgb = 0x3366CC;
        std::uint32_t id = 0;   // Cubase channel ID hash (0 = host doesn't report one)
        int automationPointsWritten = 0;
        bool panIgnored = false;   // no working pan (a mono track into a mono group): pan writes don't land
    };

    std::vector<Channel> channels;
    int bankSize = 8;
    int bankOffset = 0;
    int scriptVersion = 1;
    bool echoPanWrites = false;   // script v1 - v3: reports the pan it sent, even where no panner takes it
    double quantum = 1.0 / 16384.0;   // Cubase may quantise positions; pretend it does
    bool alive = true;
    std::vector<std::vector<std::uint8_t>> toPlugin;
    int setVolumeCount = 0;

    // Some made-up but realistic curve: +6 dB at 1, 0 dB near 0.78, -inf at 0.
    static double lawDb (double n)
    {
        if (n <= 0.0) return -1000.0;
        return 6.0 + 20.0 * std::log10 (std::pow (n, 2.7) * (1.0 + 0.15 * (1.0 - n)));
    }
    static std::string displayText (double n)
    {
        const double db = lawDb (n);
        if (db < -96.0) return "-oo";
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.2f", db);
        return buf;
    }
    static std::string panText (double p)
    {
        const int v = static_cast<int> (std::lround ((p - 0.5) * 200.0));
        if (v == 0) return "C";
        return (v < 0 ? "L" : "R") + std::to_string (std::abs (v));
    }

    Channel* inSlot (int slot)
    {
        const int i = bankOffset + slot;
        if (slot < 0 || slot >= bankSize || i < 0 || i >= static_cast<int> (channels.size())) return nullptr;
        return &channels[static_cast<std::size_t> (i)];
    }

    void out (const mixology::bridge::Message& m) { if (alive) toPlugin.push_back (mixology::bridge::encode (m)); }

    void sendSlot (int slot)
    {
        using namespace mixology::bridge;
        auto* c = inSlot (slot);
        Message t; t.cmd = Cmd::slotTitle; t.slot = slot;
        t.text = c ? c->name : std::string(); t.hash = c ? nameHash (c->name) : 0u; t.channelId = c ? c->id : 0u;
        out (t);
        if (! c) return;
        sendVolume (slot);
        Message p; p.cmd = Cmd::slotPan; p.slot = slot; p.value = c->pan; p.text = panText (c->pan); out (p);
        Message mu; mu.cmd = Cmd::slotMute; mu.slot = slot; mu.flag = c->mute; out (mu);
        Message so; so.cmd = Cmd::slotSolo; so.slot = slot; so.flag = c->solo; out (so);
        Message co; co.cmd = Cmd::slotColour; co.slot = slot; co.rgb = c->rgb; co.flag = 1; out (co);
        Message au; au.cmd = Cmd::slotAutomation; au.slot = slot; au.flag = (c->read ? 1 : 0) | (c->write ? 2 : 0); out (au);
    }
    void sendVolume (int slot)
    {
        auto* c = inSlot (slot);
        if (! c) return;
        mixology::bridge::Message v; v.cmd = mixology::bridge::Cmd::slotVolume; v.slot = slot;
        v.value = c->volume; v.text = displayText (c->volume);
        out (v);
    }
    void dump() { for (int s = 0; s < bankSize; ++s) sendSlot (s); }

    void activate()   // Cubase starts / script reloads: it announces itself
    {
        mixology::bridge::Message a; a.cmd = mixology::bridge::Cmd::helloAck; a.flag = scriptVersion; a.slot = bankSize;
        out (a);
    }

    void receive (const std::vector<std::uint8_t>& bytes)
    {
        using namespace mixology::bridge;
        if (! alive) return;
        auto m = decode (bytes);
        if (! m) return;
        auto ack = [&] (AckStatus s) { Message a; a.cmd = Cmd::ack; a.token = m->token; a.flag = static_cast<int> (s); out (a); };

        switch (m->cmd)
        {
            case Cmd::hello: { Message a; a.cmd = Cmd::helloAck; a.flag = scriptVersion; a.slot = bankSize; a.token = m->token; out (a); break; }
            case Cmd::requestDump: dump(); break;
            case Cmd::ping: { Message p; p.cmd = Cmd::pong; p.token = m->token; out (p); break; }
            case Cmd::bank:
            {
                const auto action = static_cast<BankAction> (m->flag);
                if (action == BankAction::next) bankOffset += bankSize;
                if (action == BankAction::previous) bankOffset = std::max (0, bankOffset - bankSize);
                if (action == BankAction::shiftRight) bankOffset += 1;
                if (action == BankAction::shiftLeft) bankOffset = std::max (0, bankOffset - 1);
                if (action == BankAction::reset) bankOffset = 0;
                dump();
                break;
            }
            case Cmd::setVolume: case Cmd::setPan: case Cmd::setMute: case Cmd::setSolo:
            {
                auto* c = inSlot (m->slot);
                if (! c) { ack (AckStatus::emptySlot); break; }
                if (nameHash (c->name) != m->hash || c->id != m->channelId) { ack (AckStatus::nameMismatch); break; }
                if (c->write) { ack (AckStatus::automationWrite); break; }
                ack (AckStatus::applied);
                if (m->cmd == Cmd::setVolume)
                {
                    ++setVolumeCount;
                    c->volume = std::round (m->value / quantum) * quantum;
                    sendVolume (m->slot);
                }
                else if (m->cmd == Cmd::setPan)
                {
                    if (! c->panIgnored) c->pan = m->value;
                    const double shown = echoPanWrites ? m->value : c->pan;
                    Message p; p.cmd = Cmd::slotPan; p.slot = m->slot; p.value = shown; p.text = panText (shown); out (p);
                }
                else if (m->cmd == Cmd::setMute)
                {
                    c->mute = m->flag != 0;
                    Message p; p.cmd = Cmd::slotMute; p.slot = m->slot; p.flag = c->mute; out (p);
                }
                else
                {
                    c->solo = m->flag != 0;
                    Message p; p.cmd = Cmd::slotSolo; p.slot = m->slot; p.flag = c->solo; out (p);
                }
                break;
            }
            default: break;
        }
    }

    // User actions inside Cubase
    void userMovesFader (int channel, double n)
    {
        channels[static_cast<std::size_t> (channel)].volume = n;
        const int slot = channel - bankOffset;
        if (slot >= 0 && slot < bankSize) sendVolume (slot);
    }
    // What Cubase Artist 14 can do while a fader is dragged: report the new position with the previous
    // position's text; the matching text only comes with the next report.
    void userDragsWithLaggingText (int channel, double from, double to, int steps, std::vector<double>* times = nullptr)
    {
        (void) times;
        auto& c = channels[static_cast<std::size_t> (channel)];
        const int slot = channel - bankOffset;
        double previous = c.volume;
        for (int i = 0; i <= steps; ++i)
        {
            c.volume = from + (to - from) * i / steps;
            if (slot < 0 || slot >= bankSize) continue;
            mixology::bridge::Message v; v.cmd = mixology::bridge::Cmd::slotVolume; v.slot = slot;
            v.value = c.volume; v.text = displayText (previous);
            out (v);
            previous = c.volume;
        }
    }

    void userMovesPan (int channel, double p)
    {
        channels[static_cast<std::size_t> (channel)].pan = p;
        const int slot = channel - bankOffset;
        if (slot < 0 || slot >= bankSize) return;
        mixology::bridge::Message m; m.cmd = mixology::bridge::Cmd::slotPan; m.slot = slot; m.value = p; m.text = panText (p);
        out (m);
    }

    void rename (int channel, const std::string& name)
    {
        channels[static_cast<std::size_t> (channel)].name = name;
        const int slot = channel - bankOffset;
        if (slot >= 0 && slot < bankSize) sendSlot (slot);
    }
    void moveChannel (int from, int to)   // drag a track to another position
    {
        auto c = channels[static_cast<std::size_t> (from)];
        channels.erase (channels.begin() + from);
        channels.insert (channels.begin() + to, c);
        dump();
    }
};
