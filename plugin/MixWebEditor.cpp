#include "MixWebEditor.h"

#include "MixologyUIData.h"

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
                                 complete (true);
                             })
        .withNativeFunction ("setUiState", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 if (args.size() > 0) processor.setUiState (args[0].toString());
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

        // 4.5 dB/octave tilt around 1 kHz so a balanced mix reads roughly flat (same as Polisher).
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
    frame->setProperty ("spectrum", makeSpectrum());

    if (currentPage == "stereo" || currentPage == "master")
        addStereo (*frame);

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
