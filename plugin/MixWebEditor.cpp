#include "MixWebEditor.h"

#include "MixologyUIData.h"

#include "mixology/LinkRegistry.h"

#include <cstring>

namespace
{
    juce::String mimeTypeFor (const juce::String& file)
    {
        const auto ext = file.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "html") return "text/html";
        if (ext == "js")   return "text/javascript";
        if (ext == "css")  return "text/css";
        if (ext == "png")  return "image/png";
        if (ext == "jpg")  return "image/jpeg";
        if (ext == "webp") return "image/webp";
        if (ext == "svg")  return "image/svg+xml";
        if (ext == "ttf")  return "font/ttf";
        if (ext == "woff2") return "font/woff2";
        if (ext == "json") return "application/json";
        if (ext == "md")   return "text/markdown";
        return "text/plain";
    }

    float roundTo (float v, float step) { return std::round (v / step) * step; }

    const juce::Colour kBackground (0xff05080f);

    /** "link:<slot>:<generation>" -> slot and generation. */
    bool parseLinkKey (const juce::String& key, int& slot, std::uint32_t& generation)
    {
        if (! key.startsWith ("link:")) return false;
        const auto parts = juce::StringArray::fromTokens (key.substring (5), ":", "");
        if (parts.size() != 2) return false;
        slot = parts[0].getIntValue();
        generation = static_cast<std::uint32_t> (parts[1].getLargeIntValue());
        return slot >= 0;
    }

    juce::String linkKey (const mixology::link::LinkInfo& l)
    {
        return "link:" + juce::String (l.slot) + ":" + juce::String (static_cast<juce::int64> (l.generation));
    }

    juce::Array<juce::var> moduleKeys (const mixology::proc::Values& v, bool runningOnly, std::uint32_t running)
    {
        juce::Array<juce::var> out;
        for (int m = 0; m < mixology::proc::kModules; ++m)
        {
            const auto mod = static_cast<mixology::proc::Module> (m);
            const bool on = runningOnly ? ((running >> m) & 1u) != 0 : v[static_cast<std::size_t> (mixology::proc::onParam (mod))] > 0.5f;
            if (on) out.add (mixology::proc::moduleKey (mod));
        }
        return out;
    }
}

juce::WebBrowserComponent::Options MixWebEditor::makeBaseOptions()
{
    using Options = juce::WebBrowserComponent::Options;
    return Options{}
       #if JUCE_WINDOWS
        .withBackend (Options::Backend::webview2)
       #endif
        .withWinWebView2Options (Options::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                              .getChildFile ("MixologyWebView"))
                                     .withBackgroundColour (kBackground)
                                     .withStatusBarDisabled())
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled();
}

bool MixWebEditor::isSupported()
{
    return juce::WebBrowserComponent::areOptionsSupported (makeBaseOptions());
}

juce::WebBrowserComponent::Options MixWebEditor::makeEditorOptions()
{
    return makeBaseOptions()
        .withNativeFunction ("bridgeCommand", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 const auto name = args.size() > 0 ? args[0].toString() : juce::String();
                                 const auto arg = args.size() > 1 ? args[1] : juce::var();
                                 auto result = hub.command (name, arg);
                                 sentBridgeVersion = ~0ull;   // push fresh state with the next frame
                                 complete (result);
                             })
        .withNativeFunction ("getBridgeState", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (hub.getState (true));
                             })
        .withNativeFunction ("getSettings", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 auto* o = new juce::DynamicObject();
                                 o->setProperty ("theme", hub.settings().getValue ("theme", "dark"));
                                 o->setProperty ("ui", processor.getUiState());
                                 o->setProperty ("version", JucePlugin_VersionString);
                                 complete (juce::var (o));
                             })
        .withNativeFunction ("setSetting", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (args.size() >= 2 && args[0].toString() == "theme")
                                 {
                                     hub.settings().setValue ("theme", args[1].toString() == "light" ? "light" : "dark");
                                     hub.settings().saveIfNeeded();
                                 }
                                 complete (true);
                             })
        .withNativeFunction ("setPage", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (args.size() > 0) currentPage = args[0].toString();
                                 if (currentPage != "stereo") processor.monoListen.store (false);
                                 complete (true);
                             })
        .withNativeFunction ("setUiState", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (args.size() > 0) processor.setUiState (args[0].toString());
                                 complete (true);
                             })
        .withNativeFunction ("setMonoListen", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 processor.monoListen.store (args.size() > 0 && static_cast<bool> (args[0]));
                                 complete (processor.monoListen.load());
                             })
        .withNativeFunction ("procTarget", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 procKey = args.size() > 0 && args[0].toString().isNotEmpty() ? args[0].toString() : juce::String ("mix");
                                 procListCountdown = 0;
                                 complete (true);
                             })
        .withNativeFunction ("procSet", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 // procSet (key, { id: value, ... }): moves on Mixology's own Full Mix processing, or
                                 // requests to a track's Link (it applies them like your own moves).
                                 const auto key = args.size() > 0 ? args[0].toString() : juce::String();
                                 int slot = -1;
                                 std::uint32_t generation = 0;
                                 const bool link = parseLinkKey (key, slot, generation);
                                 int applied = 0;
                                 if (auto* changes = args.size() > 1 ? args[1].getDynamicObject() : nullptr)
                                     for (const auto& p : changes->getProperties())
                                     {
                                         const int index = mixology::proc::indexOf (p.name.toString().toStdString());
                                         if (index < 0) continue;
                                         const auto value = static_cast<float> (static_cast<double> (p.value));
                                         if (key == "mix") processor.setProc (index, value);
                                         else if (link) mixology::link::processRegistry().requestProc (slot, generation, index, value);
                                         else continue;
                                         ++applied;
                                     }
                                 complete (applied);
                             })
        .withNativeFunction ("spaceDuck", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 // spaceDuck (key, [hash, ...]): the tracks a Link's Space effect ducks under.
                                 const auto key = args.size() > 0 ? args[0].toString() : juce::String();
                                 int slot = -1;
                                 std::uint32_t generation = 0;
                                 if (! parseLinkKey (key, slot, generation)) { complete (false); return; }
                                 mixology::link::DuckSources d;
                                 if (auto* list = args.size() > 1 ? args[1].getArray() : nullptr)
                                     for (const auto& h : *list)
                                         if (d.count < mixology::link::kMaxDuckSources)
                                             if (const auto hash = static_cast<std::uint32_t> (static_cast<juce::int64> (h)); hash != 0)
                                                 d.hashes[static_cast<std::size_t> (d.count++)] = hash;
                                 mixology::link::processRegistry().requestDuck (slot, generation, d);
                                 procListCountdown = 0;   // the list shows it with the next frames
                                 complete (true);
                             })
        .withResourceProvider ([] (const auto& url) { return getResource (url); });
}

MixWebEditor::MixWebEditor (MixologyAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      hub (*p.bridgeHub),
      webView (makeEditorOptions())
{
    hub.enable();

    addAndMakeVisible (webView);
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // Fixed 4:3 design (the approved mockups), freely resizable; the page scales itself to fit.
    setResizable (true, true);
    setResizeLimits (720, 540, 2160, 1620);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kDesignWidth) / kDesignHeight);
    setSize (1152, 864);

    startTimerHz (30);
}

MixWebEditor::~MixWebEditor()
{
    stopTimer();
    processor.monoListen.store (false);   // Mono Check never outlives the window
}

void MixWebEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void MixWebEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> MixWebEditor::getResource (const juce::String& url)
{
    auto path = url == "/" ? juce::String ("index.html") : url.fromFirstOccurrenceOf ("/", false, false);
    path = path.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    if (path.isEmpty())
        path = "index.html";

    // Resources are embedded flat (file names are unique), so match on the file name.
    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < MixologyUI::namedResourceListSize; ++i)
    {
        if (fileName == MixologyUI::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = MixologyUI::getNamedResource (MixologyUI::namedResourceList[i], size))
            {
                std::vector<std::byte> bytes (static_cast<size_t> (size));
                std::memcpy (bytes.data(), data, bytes.size());
                return juce::WebBrowserComponent::Resource { std::move (bytes), mimeTypeFor (fileName) };
            }
        }
    }
    return std::nullopt;
}

juce::var MixWebEditor::makeSpectrum()
{
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    processor.spectrumTap.copyLatest (fftBuffer.data(), kFftSize);
    window.multiplyWithWindowingTable (fftBuffer.data(), static_cast<size_t> (kFftSize));
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const double sampleRate = juce::jmax (8000.0, processor.getSampleRate());
    const double binHz = sampleRate / kFftSize;
    const float fullScale = kFftSize * 0.25f;   // Hann-windowed full-scale sine
    constexpr int lastBin = kFftSize / 2 - 1;

    juce::Array<juce::var> out;
    out.ensureStorageAllocated (kSpectrumPoints);
    for (int i = 0; i < kSpectrumPoints; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, static_cast<double> (i) / (kSpectrumPoints - 1));
        const int lo = juce::jlimit (1, lastBin, static_cast<int> (std::floor (f * std::pow (2.0, -1.0 / 12.0) / binHz)));
        const int hi = juce::jlimit (lo, lastBin, static_cast<int> (std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz)));

        double power = 0.0;
        for (int b = lo; b <= hi; ++b)
            power += static_cast<double> (fftBuffer[static_cast<size_t> (b)]) * fftBuffer[static_cast<size_t> (b)];
        const double mag = std::sqrt (power / (hi - lo + 1)) / fullScale;

        // 4.5 dB/octave tilt around 1 kHz so a balanced mix reads roughly flat (same as Masterology).
        float db = static_cast<float> (20.0 * std::log10 (mag + 1.0e-9) + 4.5 * std::log2 (f / 1000.0));
        db = juce::jlimit (-120.0f, 18.0f, db);

        auto& s = smoothed[static_cast<size_t> (i)];
        if (! primed)   s = db;
        else if (db > s) s = 0.55f * db + 0.45f * s;
        else             s = 0.88f * s + 0.12f * db;
        out.add (roundTo (s, 0.1f));
    }
    primed = true;
    return out;
}

void MixWebEditor::timerCallback()
{
    auto& m = processor.meters;

    auto* frame = new juce::DynamicObject();
    frame->setProperty ("peakL", roundTo (m.peakLeft.exchange (-100.0f), 0.1f));
    frame->setProperty ("peakR", roundTo (m.peakRight.exchange (-100.0f), 0.1f));
    frame->setProperty ("momentary", roundTo (m.momentary.load(), 0.1f));
    frame->setProperty ("shortTerm", roundTo (m.shortTerm.load(), 0.1f));
    frame->setProperty ("integrated", roundTo (m.integrated.load(), 0.1f));
    frame->setProperty ("truePeak", roundTo (m.truePeak.load(), 0.1f));
    frame->setProperty ("sr", processor.getSampleRate());
    frame->setProperty ("mono", processor.monoListen.load());
    frame->setProperty ("spectrum", makeSpectrum());

    if (currentPage == "stereo" || currentPage == "master")
        addStereo (*frame);

    if (currentPage == "tracks")
        frame->setProperty ("trackMeters", hub.trackMeters());   // from each track's Mixology Link

    if (currentPage == "space")
    {
        // Tempo and beat position, brought up to now (the host reports them per audio block).
        auto& b = processor.beat;
        const bool playing = processor.transportPlaying.load();
        const double bpm = b.bpm.load();
        double ppq = b.ppq.load();
        const auto age = juce::Time::currentTimeMillis() - b.whenMs.load();
        if (playing && bpm > 0.0 && age >= 0 && age < 1000) ppq += static_cast<double> (age) / 60000.0 * bpm;
        auto* bo = new juce::DynamicObject();
        bo->setProperty ("bpm", bpm);
        bo->setProperty ("ppq", ppq);
        bo->setProperty ("bar", b.barStartPpq.load());
        bo->setProperty ("num", b.numerator.load());
        bo->setProperty ("playing", playing && age < 1000);
        frame->setProperty ("beat", juce::var (bo));
    }

    if (currentPage == "master")
    {
        // The Master page's finishing knobs: the Full Mix's Final Touch (glue, width, output) and its tilt
        // (Tone Balance warmth / air in opposite directions).
        using namespace mixology::proc;
        const auto v = processor.procValues();
        const auto pm = processor.takeProcMeters();
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("finOn", v[finOn] > 0.5f && v[chainOn] > 0.5f);
        mo->setProperty ("glue", roundTo (v[finGlue], 1.0f));
        mo->setProperty ("width", roundTo (v[finWidth], 1.0f));
        mo->setProperty ("out", roundTo (v[finOut], 0.1f));
        mo->setProperty ("tbOn", v[tbOn] > 0.5f && v[chainOn] > 0.5f);
        mo->setProperty ("warmth", roundTo (v[tbWarmth], 0.1f));
        mo->setProperty ("air", roundTo (v[tbAir], 0.1f));
        mo->setProperty ("glueGr", roundTo (pm.glue, 0.1f));
        mo->setProperty ("limOn", isOn (v, Module::limit));
        if (const auto g = hub.masterology())   // M6: Masterology after Mixology, and what it aims for
        {
            auto* go = new juce::DynamicObject();
            go->setProperty ("goal", juce::String::fromUTF8 (g->goalName.c_str()));
            go->setProperty ("genre", juce::String::fromUTF8 (g->genreName.c_str()));
            go->setProperty ("target", roundTo (g->targetLufs, 0.1f));
            go->setProperty ("ceiling", roundTo (g->ceilingDbtp, 0.1f));
            go->setProperty ("inLufs", roundTo (g->inputLufs, 0.1f));
            go->setProperty ("inPeak", roundTo (g->inputPeakDbtp, 0.1f));
            mo->setProperty ("masterology", juce::var (go));
        }
        frame->setProperty ("master", juce::var (mo));
    }

    if (currentPage == "overview")
    {
        // The Overview's quick tone knobs are the Full Mix EQ's six band gains.
        const auto v = processor.procValues();
        juce::Array<juce::var> eq;
        for (int b = 0; b < mixology::proc::kEqBands; ++b) eq.add (roundTo (v[static_cast<std::size_t> (mixology::proc::eqGain (b))], 0.01f));
        eq.add (v[mixology::proc::eqOn] > 0.5f && v[mixology::proc::chainOn] > 0.5f);
        frame->setProperty ("mixEq", eq);
    }

    if (currentPage == "eq" || currentPage == "dynamics" || currentPage == "tone" || currentPage == "stereo" || currentPage == "space")
    {
        if (--procListCountdown <= 0)
        {
            procListCountdown = 15;
            frame->setProperty ("procTargets", makeProcTargets());
        }
        frame->setProperty ("proc", makeProc());
    }

    // Analyze Mix and the analyzer's track colours: five times a second.
    if (++frameCount % 6 == 0)
    {
        mixL.resize (static_cast<std::size_t> (mixology::TrackAnalyzer::kSize));
        mixR.resize (mixL.size());
        processor.analysisRing.copyLatest (mixL.data(), mixR.data(), mixology::TrackAnalyzer::kSize);
        const auto mixFrame = mixAnalyzer.analyse (mixL.data(), mixR.data(), juce::jmax (8000.0, processor.getSampleRate()));
        hub.analysisTick (mixFrame, processor.transportPlaying.load(), processor.getTotalNumOutputChannels());
        if (currentPage == "overview")
            frame->setProperty ("roleCurves", hub.roleCurves (processor.getSampleRate()));
    }

    if (hub.stateVersion() != sentBridgeVersion)
    {
        sentBridgeVersion = hub.stateVersion();
        const auto state = hub.getState (false);
        const auto logVersion = static_cast<juce::int64> (state["logVersion"]);
        const bool withLog = logVersion != sentLogVersion;
        sentLogVersion = logVersion;
        frame->setProperty ("bridge", withLog ? hub.getState (true) : state);
    }

    webView.emitEventIfBrowserIsVisible ("mixFrame", juce::var (frame));
}

juce::var MixWebEditor::makeProcTargets()
{
    using namespace mixology;
    juce::Array<juce::var> list;
    targetInfo.clear();

    auto add = [&] (const juce::String& key, const juce::String& name, const juce::String& colour, const proc::Values& v,
                    std::uint32_t running, const TargetInfo& info) {
        auto* o = new juce::DynamicObject();
        o->setProperty ("key", key);
        o->setProperty ("name", name);
        o->setProperty ("colour", colour);
        o->setProperty ("channels", info.channels);
        o->setProperty ("on", moduleKeys (v, false, 0));
        o->setProperty ("running", moduleKeys (v, true, running));
        o->setProperty ("spaceType", juce::roundToInt (v[static_cast<std::size_t> (proc::spType)]));
        o->setProperty ("spaceRoute", juce::roundToInt (v[static_cast<std::size_t> (proc::spRoute)]));
        list.add (juce::var (o));
        targetInfo[key] = info;
        return o;
    };

    TargetInfo mix { juce::jmax (8000.0, processor.getSampleRate()), processor.getLatencySamples(), processor.getTotalNumOutputChannels(), "Full Mix" };
    add ("mix", "Full Mix", {}, processor.procValues(), 0, mix);

    auto& registry = link::processRegistry();
    const auto ms = link::nowMs();
    for (const auto& l : registry.links (ms))
    {
        if (! l.hasProcessing) continue;
        proc::Values v {};
        if (! registry.readProc (l.slot, l.generation, v)) continue;
        const auto name = l.identity.hasName ? juce::String::fromUTF8 (l.identity.name.c_str()) : juce::String ("Track without a name");
        const auto colour = l.identity.hasColour ? "#" + juce::String::toHexString (static_cast<int> (l.identity.rgb)).paddedLeft ('0', 6) : juce::String();
        auto* o = add (linkKey (l), name, colour, v, l.procRunning,
                       { l.sampleRate > 0.0f ? static_cast<double> (l.sampleRate) : 48000.0, l.latency, l.channels > 0 ? l.channels : 2, name });
        // For the Space page: who this track is (ducking picks tracks by name), its role, what it ducks under.
        o->setProperty ("hash", static_cast<juce::int64> (l.nameHash));
        if (l.identity.hasName)
            if (auto role = hub.trackRole (l.nameHash, l.identity.name)) o->setProperty ("role", mix::roleKey (*role));
        juce::Array<juce::var> duck;
        for (int i = 0; i < l.duck.count; ++i) duck.add (static_cast<juce::int64> (l.duck.hashes[static_cast<std::size_t> (i)]));
        o->setProperty ("duck", duck);
    }
    return list;
}

juce::var MixWebEditor::trackSpectrum (const mixology::AnalysisFrame& f) const
{
    // The track's 1/3-octave bands in the units of the mix spectrum (per FFT bin, tilted 4.5 dB per octave),
    // spread over the same 120 points (20 Hz - 20 kHz).
    using namespace mixology;
    const double binHz = std::max (8000.0, processor.getSampleRate()) / 8192.0;
    std::array<double, kBands> db {};
    for (int b = 0; b < kBands; ++b)
    {
        const double hz = bandHz (b);
        db[static_cast<std::size_t> (b)] = f.bandDb[static_cast<std::size_t> (b)] - 10.0 * std::log10 (std::max (1.0, 0.2316 * hz / binHz))
                                           + 4.5 * std::log2 (hz / 1000.0);
    }
    juce::Array<juce::var> out;
    for (int i = 0; i < kSpectrumPoints; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, static_cast<double> (i) / (kSpectrumPoints - 1));
        const double pos = std::clamp (3.0 * std::log2 (hz / 1000.0) + 17.0, 0.0, static_cast<double> (kBands - 1));
        const int b = std::min (kBands - 2, static_cast<int> (pos));
        const double t = pos - b;
        out.add (roundTo (static_cast<float> (db[static_cast<std::size_t> (b)] * (1.0 - t) + db[static_cast<std::size_t> (b + 1)] * t), 0.1f));
    }
    return out;
}

juce::var MixWebEditor::makeProc()
{
    using namespace mixology;
    auto* o = new juce::DynamicObject();
    o->setProperty ("key", procKey);
    proc::Values v {};
    proc::Meters m;
    bool ok = false, metersOk = false;
    if (procKey == "mix")
    {
        v = processor.procValues();
        m = processor.takeProcMeters();
        ok = metersOk = true;
        targetInfo[procKey] = { juce::jmax (8000.0, processor.getSampleRate()), processor.getLatencySamples(), processor.getTotalNumOutputChannels(), "Full Mix" };
    }
    else
    {
        int slot = -1;
        std::uint32_t generation = 0;
        if (parseLinkKey (procKey, slot, generation))
        {
            auto& registry = link::processRegistry();
            ok = registry.readProc (slot, generation, v);
            metersOk = ok && registry.readProcMeters (slot, generation, m, link::nowMs());
            AnalysisFrame f;
            bool playing = false;
            std::int64_t pos = 0;
            std::uint64_t when = 0;
            if (ok && registry.readAnalysis (slot, f, playing, pos, when) && link::nowMs() - std::min (link::nowMs(), when) < 1000 && ! f.silent)
            {
                o->setProperty ("spectrum", trackSpectrum (f));
                o->setProperty ("corr", roundTo (f.correlation, 0.01f));   // the track's own stereo (the scope shows the mix)
            }
        }
    }
    o->setProperty ("ok", ok);
    if (! ok) return juce::var (o);

    juce::Array<juce::var> values;
    for (auto x : v) values.add (roundTo (x, 0.001f));
    o->setProperty ("values", values);
    const auto info = targetInfo.count (procKey) ? targetInfo[procKey] : TargetInfo {};
    o->setProperty ("sr", info.sampleRate);
    o->setProperty ("channels", info.channels);
    o->setProperty ("latencyMs", roundTo (static_cast<float> (info.latency * 1000.0 / info.sampleRate), 0.1f));
    if (metersOk)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("inL", roundTo (m.inL, 0.1f));
        mo->setProperty ("inR", roundTo (m.inR, 0.1f));
        mo->setProperty ("outL", roundTo (m.outL, 0.1f));
        mo->setProperty ("outR", roundTo (m.outR, 0.1f));
        mo->setProperty ("comp", roundTo (m.comp, 0.1f));
        mo->setProperty ("multi", juce::Array<juce::var> { roundTo (m.multiLow, 0.1f), roundTo (m.multiMid, 0.1f), roundTo (m.multiHigh, 0.1f) });
        mo->setProperty ("trans", roundTo (m.trans, 0.1f));
        mo->setProperty ("gate", roundTo (m.gate, 0.1f));
        mo->setProperty ("limit", roundTo (m.limit, 0.1f));
        mo->setProperty ("clip", roundTo (m.clip, 0.1f));
        mo->setProperty ("glue", roundTo (m.glue, 0.1f));
        mo->setProperty ("space", roundTo (m.spaceWet, 0.1f));   // the effect's level (dBFS peak)
        mo->setProperty ("duck", roundTo (m.duck, 0.1f));        // dB it is ducked under the vocal now
        mo->setProperty ("running", moduleKeys (v, true, m.running));
        o->setProperty ("meters", juce::var (mo));
    }
    return juce::var (o);
}

void MixWebEditor::addStereo (juce::DynamicObject& frame)
{
    const int n = static_cast<int> (stereoL.size());
    processor.stereoTap.copyLatest (stereoL.data(), stereoR.data(), n);

    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = 0; i < n; ++i)
    {
        lr += static_cast<double> (stereoL[static_cast<size_t> (i)]) * stereoR[static_cast<size_t> (i)];
        ll += static_cast<double> (stereoL[static_cast<size_t> (i)]) * stereoL[static_cast<size_t> (i)];
        rr += static_cast<double> (stereoR[static_cast<size_t> (i)]) * stereoR[static_cast<size_t> (i)];
    }
    const bool silent = ll + rr < 1.0e-7;
    const float corr = silent ? 0.0f : static_cast<float> (lr / std::sqrt (std::max (ll * rr, 1.0e-20)));
    correlationSmoothed = 0.8f * correlationSmoothed + 0.2f * corr;

    // Vectorscope dots: side across, mid up (unit = full scale). 512 dots from the latest 2048 frames.
    juce::Array<juce::var> dots;
    dots.ensureStorageAllocated (1024);
    constexpr float k = 0.70710678f;
    for (int i = 0; i < n; i += 4)
    {
        const float l = stereoL[static_cast<size_t> (i)], r = stereoR[static_cast<size_t> (i)];
        dots.add (roundTo ((l - r) * k, 0.001f));
        dots.add (roundTo ((l + r) * k, 0.001f));
    }
    frame.setProperty ("scope", dots);
    frame.setProperty ("corr", silent ? juce::var() : juce::var (roundTo (correlationSmoothed, 0.01f)));
}

MixFallbackEditor::MixFallbackEditor (MixologyAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    setSize (640, 240);
}

void MixFallbackEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff05080f));
    g.setColour (juce::Colour (0xffe8c46a));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("MIXOLOGY", getLocalBounds().removeFromTop (90), juce::Justification::centred);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText ("Mixology's interface needs the Microsoft Edge WebView2 Runtime.\n"
                      "Install it (free, from microsoft.com/edge/webview2), then reopen this window.\n"
                      "Audio passes through untouched in the meantime.",
                      getLocalBounds().reduced (24).withTrimmedTop (70), juce::Justification::centredTop, 4);
}
