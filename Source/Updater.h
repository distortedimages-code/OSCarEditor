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
    // Release notes written in both languages ("## English" and "## Français" sections): keeps the
    // section for 'language' ("en" or "fr") and drops the headings. Other notes come back unchanged.
    static juce::String notesForLanguage (const juce::String& notes, const juce::String& language);

    // Replaces each existing bundle in 'targets' with the OXIde bundle of the same format found in
    // the zip, in the same folder; a target still named OSCar Editor is removed once its OXIde
    // replacement is in. 'extract' unpacks the zip into a folder (ditto on macOS). Each replacement
    // is atomic per bundle: the old one is put back if the new one can't be moved in.
    //
    // A folder the plug-in can't write in (/Library/Audio/Plug-Ins/Components when the bundle was
    // installed with the administrator password): the new bundle is unpacked and checked in a
    // temporary folder, then 'elevate' runs one shell script that swaps all such bundles at once
    // (macOS asks the password once). Without 'elevate' those bundles are reported and left as they
    // are. 'writable' tells whether a folder can be written in (tests); by default, trying is enough.
    using Extractor = std::function<bool (const juce::File& zip, const juce::File& destination)>;
    using Elevator = std::function<bool (const juce::String& shellScript)>;
    static juce::Result installBundles (const juce::File& zip, const juce::Array<juce::File>& targets,
                                        const juce::String& expectedVersion, const Extractor& extract,
                                        juce::StringArray& installed, const Elevator& elevate = {},
                                        const std::function<bool (const juce::File&)>& writable = {});
    static juce::String shellQuote (const juce::String& s);   // 'like this', for /bin/sh
    static juce::Array<juce::File> installedBundles();   // where the plug-in is installed on this Mac (either name)
    // Copies still carrying the pre-0.5 name while the same format is installed as OXIde
    // somewhere: two copies of one plug-in, which hosts confuse.
    static juce::Array<juce::File> outdatedCopies();
    static juce::Array<juce::File> outdatedCopies (const juce::Array<juce::File>& installed);

    static constexpr const char* productName = "OXIde";          // bundle names from 0.5
    static constexpr const char* legacyName  = "OSCar Editor";   // bundle names before 0.5
    // The zip the updater downloads. Releases also carry OSCar-Editor-mac.zip (the same plug-in
    // under the old bundle names) for the updaters of 0.4 and older.
    static constexpr const char* assetName = "OXIde-mac.zip";

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
