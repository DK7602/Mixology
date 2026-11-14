# Sonicology Rack — Conversation and UI Designs

**Date:** Friday, November 13, 2026  
**Scope:** Today's Sonicology Rack discussion only, excluding earlier plugin-advisor conversations.  
**Pictures:** Three original interface references supplied by the user and three new generated rack designs (all included).

---

## 1. User — New rack idea

> Ok, lets talk about this. I saw a plug in called rackoholic. It made me think, should we build a rack for Sonicology tools (Masterology, Voxology, mixology, and any more we add to the line up)? Would it be useful, how?

## 2. Assistant — Sonicology Rack recommendation

**Yes! I think a Sonicology Rack could be genuinely useful—and potentially become the centerpiece of your entire Sonicology suite.** But it should be designed differently from a traditional rack. The opportunity isn't simply putting Mixology, Voxology, and Masterology into one window. It's allowing them to **work together as one intelligent, modular audio-processing system.**

The inspiration mentioned was [Rackaholic](https://rackaholic.net/), described in the conversation as having 26 processing modules that users can reorder, combine, bypass, and save into effect chains.

### 1. What Sonicology Rack could be

Imagine opening **one VST3 plugin inside Cubase**, with a virtual effects rack containing *individual processing modules* from different Sonicology products. For example:

**Vocal chain:** Voxology pitch correction → Mixology EQ → Voxology compression → Mixology reverb.

Mixology and Voxology can appear more than once, because a musician chooses modules rather than loading the whole fixed chain from a product. That is the main advantage of **Design B**.

### 2. Why it would be useful

| Feature | Benefit |
|---|---|
| One plugin window | Manage a complete processing chain without multiple windows. |
| Drag-and-drop modules | Change effect order instantly. |
| Shared presets | Save complete vocal, guitar, bass, or drum processing chains. |
| Global A/B comparison | Compare processed and original versions at matched loudness. |
| Automatic gain staging | Help prevent one processor from overdriving another. |
| CPU management | Bypass unused modules and offer low-latency tracking options. |
| Smart recommendations | Suggest appropriate chains for the audio and the user's goal. |

**Caveat:** Cubase Artist already provides FX Chain Presets. A preset manager alone would not justify another plugin; modular routing, control, and intelligent analysis would make this more compelling.

### 3. Examples on different tracks

- **Lead vocal:** Pitch correction → EQ → compression → reverb. One insert on the vocal track.
- **Drum bus:** EQ → transient shaping *(if implemented)* → saturation → bus compression. One insert on the drum bus.
- **Mastering:** Tonal balance → compression → stereo control → limiting → loudness analysis. One insert on Stereo Out. Masterology should generally remain on the mastering bus.

### 4. The feature that could differentiate it: intelligent coaching

The earlier **plugin advisor** concept could live *inside* the rack as an **Analyze & Build** button. A musician inserts the rack on a vocal track and plays representative audio. The advisor could detect sibilance, uneven dynamics, or low-mid buildup, suggest a chain of Sonicology modules, explain why, and let the user audition it. Later enhancements might include reference-track matching, genre/style-based recommendations, and cross-track analysis.

**Technical limitation:** A plugin on one Cubase channel does not automatically receive every other channel. Cross-track recommendations would need additional audio routing, multiple communicating instances, or a companion app.

### 5. Two possible implementation architectures

**Option A — Host the complete existing plugins:** a rack that loads full Mixology, Voxology, and Masterology VST3 instances. It retains existing UIs, but nesting plugins introduces compatibility, hosting, and state-management challenges.

**Option B — A shared modular processing engine (preferred):** make the DSP processors reusable modules available to both Sonicology Rack and the individual plugins. More engineering work at first, but it enables a genuinely reorderable modular rack and cleanly supports future additions.

The standalone versions of Mixology, Voxology, and Masterology should remain available.

### 6. Suggested development stages

1. **Unified Rack:** add, arrange, bypass, level-match, and save processor chains.
2. **Advanced Rack:** individual processing modules, parallel processing, macros, and shared presets.
3. **Intelligent Rack:** audio diagnosis, recommended chains, reference matching, and coached settings.

**Design decision offered:** A = complete plugin rack; B = individual modules; C = both. The assistant preferred **B**.

---

## 3. User — Screenshots and selected direction

The user uploaded these three existing interface screenshots:

1. **Voxology:** gold/cream honeycomb styling, vocal chain blocks, pitch correction controls, vocal spectrum, and Learn panel.
2. **Mixology:** a dark/neon audio FX interface with blue/red/gold highlights, multiple module tabs, special FX artwork, controls, and ducking.
3. **Masterology:** screenshot of the mastering interface still titled **Polisher**, with blue/red chrome, a vertically ordered mastering chain, analyzer, maximizer, and explanatory controls.

**User's exact request:**

> I want design B also! Please design the ui of the rack and the module selection page and the auto rack/ coaching page.

The original screenshots are files `images/01_...` through `images/03_...`, and appear as full pages in the PDF.

## 4. Assistant — Three Sonicology Rack UI concept images

The assistant generated **three static UI mockups**. They are design illustrations, not running software, and their simulated meters, audio analysis, and improvement scores are not measured results.

### A. Main Sonicology Rack interface

**Image:** `images/04_Sonicology_Rack_main_UI.png`

- A shared Sonicology Rack brand bar with preset selection, A/B, bypass, save, analyze, and power.
- Left vertical **SIGNAL CHAIN**: modules such as Pitch, Cleanup, Tone EQ, Dynamic EQ, De-Esser, Rider, Compressor, Saturation, Stereo, Delay, Reverb, and Output. Each block has a power toggle and a reorder handle.
- Center **CHAIN FLOW** showing the order of modules, with a selected module's detailed editor below. The illustration shows **Dynamic EQ** with spectral graph and frequency, gain, Q, range, attack, release, threshold, ratio, and detection controls.
- Right **RACK COACH** explaining the selected processing chain, plus practical tips.
- Bottom input/output loudness meters.

### B. Module selection / browser interface

**Image:** `images/05_Sonicology_Rack_module_browser.png`

- Search and category filtering (Vocal, Mixing, Mastering, Utility, Creative, Dynamics, Space).
- Card browser of modules sourced from the suite, including Pitch, Cleanup, Tone EQ, Dynamic EQ, De-Esser, Rider, Compressor, Saturation, Stereo, Delay, Reverb, Width, Limiter, Tone Steady, Resonance Tamer, Bass Control, Glue, Soft Clipper, Maximizer, Hall, Room, Chorus, and Phaser.
- Right panel previews a selected module and explains use cases, controls, and insertion position.
- Buttons to add the selected module, load templates, favorite modules, or create a new chain.

**Implementation note:** The visual source tags and modules are design proposals, not independently verified as already existing in each codebase.

### C. Auto Rack / coaching interface

**Image:** `images/06_Sonicology_Rack_auto_coach.png`

- Choose Source, Style/Genre, Goal/Intensity, and optional Reference.
- Primary **ANALYZE & BUILD** action.
- Issue-focused spectral analysis identifying possible mud, harshness, sibilance, inconsistent dynamics, or lack of air.
- A recommended visual chain with each module's purpose explained.
- Coaching sections: **WHAT I HEARD**, **WHY I BUILT THIS RACK**, and **ADJUST FIRST (QUICK TIPS)**.
- Before/Proposed visual comparison, illustrative estimated metrics, and buttons for Build Rack, Apply to Track, Save as Preset, and Tweak Manually.

---

## 5. Handoff for Claude

- **Selected design:** B, fully modular signal-chain construction using individual processor modules.
- **Core products:** Mixology, Voxology, and Masterology, with expandability for additional Sonicology products.
- **DAW:** Cubase Artist and VST3 integration.
- **Principal benefit:** choose individual modules from all Sonicology tools in one rack, with intelligent explanations of audio-processing choices.
- **Keep independent apps:** Sonicology Rack should complement, not replace, standalone Mixology, Voxology, and Masterology.
- **Needs technical assessment:** reusability of existing DSP code, VST3 host constraints, plugin-state serialization, parameter automation, cross-track input routing, CPU/latency, gain staging, and realistic analysis/coach capabilities.
- **UI aesthetic:** unified dark navy, chrome red/blue, gold honeycomb/glass language inspired by the three reference screenshots.

## Images (six files included at original resolution)

### 1. Voxology reference
![Voxology reference](images/01_Voxology_reference.png)

### 2. Mixology reference
![Mixology reference](images/02_Mixology_reference.png)

### 3. Masterology (earlier Polisher label) reference
![Masterology reference](images/03_Masterology_reference.png)

### 4. Sonicology Rack main screen
![Sonicology Rack main UI](images/04_Sonicology_Rack_main_UI.png)

### 5. Sonicology Rack module browser
![Sonicology Rack module browser UI](images/05_Sonicology_Rack_module_browser.png)

### 6. Sonicology Rack Auto Coach
![Sonicology Rack Auto Coach UI](images/06_Sonicology_Rack_auto_coach.png)
