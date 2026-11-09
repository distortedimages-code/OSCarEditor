// Checks the project's GitHub releases and, on macOS, installs a newer build in place:
// the AU, VST3 and app found on this Mac are replaced by the ones in the release zip,
// after the zip's SHA-256 has been checked against the checksum published with it.
// Because the plugin downloads the file itself (not a browser), macOS doesn't
// quarantine it, so there is no Gatekeeper step. The DAW must be restarted to load it.
//
// The repository is set at build time: cmake -DOSCAR_UPDATE_REPO=owner/repo
// Releases must be public. Only api.github.com and GitHub's download host are contacted.

#pragma once

#include <JuceHeader.h>

class Updater
{
public:
    struct Release
    {
        juce::String version, title, notes, pageUrl, zipUrl, shaUrl;
    };

    enum class State { Disabled, Idle, Checking, UpToDate, Available, Installing, Installed, Failed };

    explicit Updater (std::function<juce::PropertiesFile&()> settings);
    ~Updater();

    void checkIfDue();                 // automatic check: at most once a day
    void checkNow();
    void install();                    // download, verify, replace (macOS); elsewhere opens the page
    void skipThisVersion();
    void openReleasePage() const;

    bool getAutoCheck() const;
    void setAutoCheck (bool);

    State getState() const             { return state; }
    juce::String getStatusText() const { return status; }
    const Release& getRelease() const  { return release; }

    juce::ChangeBroadcaster changes;

    static juce::String currentVersion();
    static juce::String repository();

    //==============================================================================
    // Pieces kept free of networking so they can be tested.
    static bool isNewer (const juce::String& candidate, const juce::String& current);
    static std::optional<Release> parseLatestRelease (const juce::String& json, const juce::String& assetName);
    static juce::String expectedSha (const juce::String& shaFileText);

    // Replaces each existing bundle in 'targets' with the bundle of the same name found in
    // the zip. 'extract' unpacks the zip into a folder (ditto on macOS). Each replacement is
    // atomic per bundle: the old one is put back if the new one can't be moved in.
    using Extractor = std::function<bool (const juce::File& zip, const juce::File& destination)>;
    static juce::Result installBundles (const juce::File& zip, const juce::Array<juce::File>& targets,
                                        const juce::String& expectedVersion, const Extractor& extract,
                                        juce::StringArray& installed);
    static juce::Array<juce::File> installedBundles();   // where OSCar Editor is installed on this Mac

    static constexpr const char* assetName = "OSCar-Editor-mac.zip";

private:
    void setState (State s, const juce::String& text);

    std::function<juce::PropertiesFile&()> settings;
    State state = State::Idle;
    juce::String status;
    Release release;
    bool automatic = false;

    std::atomic<bool> cancel { false };
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Updater)
    JUCE_DECLARE_NON_COPYABLE (Updater)
};
