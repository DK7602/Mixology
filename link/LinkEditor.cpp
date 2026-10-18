#include "LinkEditor.h"

#include "MixologyLinkUIData.h"
#include "suite/SuiteLink.h"

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
        if (ext == "ttf")  return "font/ttf";
        return "text/plain";
    }

    float roundTo (float v, float step) { return std::round (v / step) * step; }

    const juce::Colour kBackground (0xff05080f);
}

juce::WebBrowserComponent::Options LinkEditor::makeBaseOptions()
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
        .withNativeIntegrationEnabled();
}

bool LinkEditor::isSupported()
{
    return juce::WebBrowserComponent::areOptionsSupported (makeBaseOptions());
}

// Mixology's look (light / dark) lives in its settings file; the Link follows it.
juce::String LinkEditor::savedTheme()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Mixology";
    o.filenameSuffix = ".settings";
    o.folderName = "Mixology";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::PropertiesFile file (o);
    return file.getValue ("theme", "dark") == "light" ? "light" : "dark";
}

juce::WebBrowserComponent::Options LinkEditor::makeEditorOptions()
{
    return makeBaseOptions()
        .withNativeFunction ("getLinkInfo", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 auto* o = new juce::DynamicObject();
                                 o->setProperty ("theme", savedTheme());
                                 o->setProperty ("version", JucePlugin_VersionString);
                                 o->setProperty ("trim", processor.trimDb());
                                 // The DAW it runs in (found from the host): the window names its master channel in its words.
                                 o->setProperty ("daw", juce::String (suite::dawFromHostPath (juce::PluginHostType::getHostPath().toStdString())));
                                 complete (juce::var (o));
                             })
        .withNativeFunction ("setTrim", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (args.size() > 0) processor.setTrimDb (static_cast<float> (static_cast<double> (args[0])));
                                 complete (true);
                             })
        .withNativeFunction ("trimGesture", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (auto* p = processor.parameters.getParameter ("trim"))
                                 {
                                     if (args.size() > 0 && static_cast<bool> (args[0])) p->beginChangeGesture();
                                     else p->endChangeGesture();
                                 }
                                 complete (true);
                             })
        .withNativeFunction ("clearAssist", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 processor.setAssist (mixology::AssistSettings {});
                                 complete (true);
                             })
        .withNativeFunction ("setProcessing", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 processor.setProc (mixology::proc::chainOn, args.size() > 0 && static_cast<bool> (args[0]) ? 1.0f : 0.0f);
                                 complete (true);
                             })
        .withResourceProvider ([] (const auto& url) { return getResource (url); });
}

LinkEditor::LinkEditor (MixologyLinkProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      webView (makeEditorOptions())
{
    addAndMakeVisible (webView);
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (450, 300, 1200, 800);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kDesignWidth) / kDesignHeight);
    setSize (kDesignWidth, kDesignHeight);

    startTimerHz (30);
}

LinkEditor::~LinkEditor()
{
    stopTimer();
}

void LinkEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void LinkEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> LinkEditor::getResource (const juce::String& url)
{
    auto path = url == "/" ? juce::String ("link.html") : url.fromFirstOccurrenceOf ("/", false, false);
    path = path.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    if (path.isEmpty())
        path = "link.html";
    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < MixologyLinkUI::namedResourceListSize; ++i)
    {
        if (fileName == MixologyLinkUI::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = MixologyLinkUI::getNamedResource (MixologyLinkUI::namedResourceList[i], size))
            {
                std::vector<std::byte> bytes (static_cast<size_t> (size));
                std::memcpy (bytes.data(), data, bytes.size());
                return juce::WebBrowserComponent::Resource { std::move (bytes), mimeTypeFor (fileName) };
            }
        }
    }
    return std::nullopt;
}

void LinkEditor::timerCallback()
{
    const auto id = processor.identity();
    auto* f = new juce::DynamicObject();
    f->setProperty ("peakL", roundTo (processor.takePeak (0), 0.1f));
    f->setProperty ("peakR", roundTo (processor.takePeak (1), 0.1f));
    f->setProperty ("momentary", roundTo (processor.momentaryLufs(), 0.1f));
    f->setProperty ("shortTerm", roundTo (processor.shortTermLufs(), 0.1f));
    f->setProperty ("trim", roundTo (processor.trimDb(), 0.1f));
    f->setProperty ("name", id.hasName ? juce::var (id.name) : juce::var());
    f->setProperty ("colour", id.hasColour ? juce::var ("#" + id.colour.toDisplayString (false)) : juce::var());
    f->setProperty ("mixology", processor.mixologyOpen());
    const auto pairing = processor.pairing();
    f->setProperty ("paired", pairing == mixology::link::MatchStatus::matched ? "matched"
                                : pairing == mixology::link::MatchStatus::ambiguous ? "ambiguous" : "none");
    f->setProperty ("shared", processor.registryShared() && processor.registrySlot() >= 0);
    f->setProperty ("note", processor.lastTrimRequest());
    f->setProperty ("channels", processor.getTotalNumOutputChannels());
    const auto a = processor.assistSettings();
    auto* ao = new juce::DynamicObject();
    ao->setProperty ("lowCut", roundTo (a.lowCutHz, 1.0f));
    ao->setProperty ("mudHz", roundTo (a.mudHz, 1.0f));
    ao->setProperty ("mudDb", roundTo (a.mudDb, 0.1f));
    ao->setProperty ("pocketHz", roundTo (a.pocketHz, 1.0f));
    ao->setProperty ("pocketDb", roundTo (a.pocketDb, 0.1f));
    f->setProperty ("assist", juce::var (ao));

    // Processing: on / off, and which modules are switched on (and which are running right now).
    const auto v = processor.procValues();
    auto* po = new juce::DynamicObject();
    po->setProperty ("on", v[mixology::proc::chainOn] > 0.5f);
    juce::Array<juce::var> on, live;
    const auto runningNow = processor.procRunning();
    for (int m = 0; m < mixology::proc::kModules; ++m)
    {
        const auto mod = static_cast<mixology::proc::Module> (m);
        if (v[static_cast<std::size_t> (mixology::proc::onParam (mod))] > 0.5f) on.add (mixology::proc::moduleKey (mod));
        if ((runningNow >> m) & 1u) live.add (mixology::proc::moduleKey (mod));
    }
    po->setProperty ("modules", on);
    po->setProperty ("running", live);
    po->setProperty ("latencyMs", roundTo (static_cast<float> (processor.getLatencySamples() * 1000.0 / juce::jmax (8000.0, processor.getSampleRate())), 0.1f));
    if (v[mixology::proc::spOn] > 0.5f)   // Space: which effects (up to three), where they play, what they duck under
    {
        using namespace mixology::proc;
        auto* so = new juce::DynamicObject();
        so->setProperty ("type", juce::String (effectNames (spaceLayers (v)).c_str()));
        so->setProperty ("fx", v[spRoute] > 0.5f);
        juce::Array<juce::var> duckNames;
        if (v[duckAmount] > 0.05f)
            for (const auto& n : processor.duckNames()) duckNames.add (n);
        so->setProperty ("duck", duckNames);
        so->setProperty ("wet", roundTo (processor.spaceWetDb(), 0.1f));
        po->setProperty ("space", juce::var (so));
    }
    f->setProperty ("proc", juce::var (po));
    webView.emitEventIfBrowserIsVisible ("linkFrame", juce::var (f));
}
