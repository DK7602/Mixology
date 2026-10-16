#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/** The undo history: every change Mixology makes to your mix, step by step, so you can go back to any point and
    forward again (until you change something new: then the undone steps are gone, as in any editor).

    A step is one thing you did in Mixology: a fader drag, knobs turned on one page of one track, a preset loaded,
    Auto Mix applied, Restore... The same thing again within a few seconds adds to the same step, so a drag is one
    step, not fifty. Moves made in Cubase itself are not Mixology's and are not here.

    Framework-free and unit-tested. The hub records and plays it back (faders, pans, mute and solo through the
    Cubase bridge; processing, Assist EQ, Trim and ducking through the Links). */
namespace mixology::history
{
    enum class What { fader, pan, mute, solo, trim, proc, assist, duck };

    /** The Assist EQ's values, in the registry's order. */
    enum AssistValue : int { assistLowCutHz, assistMudHz, assistMudDb, assistPocketHz, assistPocketDb, kAssistValues };

    /** One value Mixology changed. */
    struct Change
    {
        What what = What::fader;
        std::uint32_t hash = 0;                  // the channel (its name's hash)
        bool fullMix = false;                    // Mixology's own processing (the Stereo Out)
        std::string track;                       // its name at the time ("Full Mix")
        int slot = -1;                           // a Link without a name: found by its registry slot / generation
        std::uint32_t generation = 0;
        int index = 0;                           // proc: the control (proc::Id); assist: AssistValue
        double before = 0.0, after = 0.0;        // fader dB, pan 0..1 (0.5 = centre), mute / solo 0 / 1, trim dB, value
        double beforePos = -1.0, afterPos = -1.0;   // fader: the exact position (0..1) when known
        std::vector<std::uint32_t> listBefore, listAfter;   // duck: the channels its Space ducks under

        bool sameTarget (const Change&) const;
        bool noChange() const;
        Change reversed() const;                 // before and after swapped
    };

    struct Step
    {
        std::uint64_t id = 0;
        double time = 0.0;          // when it started (the clock the hub passes: seconds since 1970)
        double lastAt = 0.0;        // the last change added to it
        std::string what;           // "Auto Mix", "EQ on 'Lead Vox 1'", "Fader 'Kick'"...
        std::string group;          // the same group again within kMergeSeconds adds to this step ("" = never)
        std::vector<Change> changes;
        bool undone = false;
        bool pending = false;       // a plan's faders are still moving in Cubase: they arrive with amend()
    };

    class History
    {
    public:
        static constexpr std::size_t kMaxSteps = 200;
        static constexpr double kMergeSeconds = 2.5;

        /** A change Mixology made. Undone steps are dropped first. Joins the newest step when it has the same group
            and came within kMergeSeconds; a value changed again there keeps its first 'before'. Changes that end up
            changing nothing are dropped, and so is a step left empty (a fader dragged away and back). Returns the
            step's id, 0 when nothing was recorded. pending: the step waits for amend() (it may start empty). */
        std::uint64_t record (const std::string& what, const std::string& group, std::vector<Change>, double now, bool pending = false);
        /** Adds changes to a step (a plan's faders, once the bridge has moved them) and ends its pending state;
            the step goes when it ends up empty. False when the step isn't there any more. */
        bool amend (std::uint64_t id, std::vector<Change>);
        void clear();

        /** Kept with the project: the steps as a block for load(). When they hold more than maxChanges changes,
            the oldest steps are left out (the project file stays small). Pending steps are kept as done. */
        static constexpr std::size_t kMaxSavedChanges = 20000;
        std::string save (std::size_t maxChanges = kMaxSavedChanges) const;
        /** The steps a project saved, in place of these. Their Links are found by name only (a Link without a name
            was only known to the session that saved it). False when the block isn't a saved history: nothing
            changes then. */
        bool load (const std::string&);

        const std::deque<Step>& steps() const { return list; }
        const Step* find (std::uint64_t id) const;
        std::uint64_t version() const { return changeCount; }

        /** The newest step that isn't undone: the mix is as it was right after it (0 = before every step). */
        std::uint64_t current() const;
        bool canUndo() const { return current() != 0; }
        bool canRedo() const;
        bool busy() const;          // a step is still pending

        /** What takes the mix to how it was right after step `id` (0 = before every step). Going back: the later
            steps' changes reversed, newest first; going forward: the undone steps up to `id`, oldest first. One
            change per value: `after` = what to set, `before` = what it should be now. Marks the steps undone or
            done; empty when there's nothing to do (or `id` isn't there). */
        std::vector<Change> moveTo (std::uint64_t id);
        std::vector<Change> undo();   // one step back
        std::vector<Change> redo();   // one step forward

    private:
        std::deque<Step> list;
        std::uint64_t nextId = 1, changeCount = 0;
        int indexOf (std::uint64_t id) const;    // -1 = not there; 0 = id 0 (the start)
        void dropUndone();
    };

    /** One line for a change: "'Kick' fader −3.2 → −5.0 dB", "'Lead Vox 1' EQ 2 Gain +2.0 → +3.5 dB". */
    std::string describe (const Change&);
    /** A step in a few words: "12 faders, 3 pans, processing on 4 tracks" (or the change itself when it's one). */
    std::string summary (const Step&);
    /** The page a processing control is on: "EQ", "Dynamics", "Tone", "Stereo", "Space" ("Processing": the on/off). */
    const char* pageOf (int controlIndex);
    /** Cubase's pan text for a pan position (0..1): "L30", "C", "R100". */
    std::string panText (double pan);
}
