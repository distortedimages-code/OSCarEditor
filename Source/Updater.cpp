#include "Updater.h"
#include "ui/Lang.h"

using lang::tr;

#ifndef OSCAR_UPDATE_REPO
 #define OSCAR_UPDATE_REPO ""
#endif
#ifndef OSCAR_VERSION
 #define OSCAR_VERSION "0.0.0"
#endif

namespace
{
    constexpr const char* kBundleId = "com.oscartools.oscareditor";

    juce::String normalise (juce::String v)
    {
        v = v.trim();
        if (v.startsWithIgnoreCase ("v"))
            v = v.substring (1);
        return v.upToFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("+", false, false);
    }

    std::array<int, 3> parts (const juce::String& v)
    {
        std::array<int, 3> p { 0, 0, 0 };
        auto tokens = juce::StringArray::fromTokens (normalise (v), ".", "");
        for (int i = 0; i < 3 && i < tokens.size(); ++i)
            p[(size_t) i] = tokens[i].getIntValue();
        return p;
    }

    // GET into memory. Returns an error text, empty on success.
    juce::String httpGet (const juce::String& url, juce::MemoryBlock& out, const std::atomic<bool>& cancel, int timeoutMs)
    {
        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: OSCar-Editor/" + Updater::currentVersion())
                           .withConnectionTimeoutMs (timeoutMs)
                           .withNumRedirectsToFollow (8)
                           .withStatusCode (&statusCode);
        auto in = juce::URL (url).createInputStream (options);
        if (in == nullptr)
            return tr ("Can't reach GitHub (offline?).");
        if (statusCode == 404)
            return tr ("No published release found.");
        if (statusCode != 0 && (statusCode < 200 || statusCode >= 300))
            return tr ("GitHub answered with error %1.", { juce::String (statusCode) });

        juce::HeapBlock<char> buffer (65536);
        for (;;)
        {
            if (cancel)
                return tr ("Cancelled.");
            const auto n = in->read (buffer.get(), 65536);
            if (n <= 0)
                break;
            out.append (buffer.get(), (size_t) n);
            if (out.getSize() > (size_t) 300 * 1024 * 1024)
                return tr ("The download is unexpectedly large: stopped.");
        }
        return {};
    }

   #if JUCE_MAC
    bool dittoExtract (const juce::File& zip, const juce::File& destination)
    {
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), destination.getFullPathName() }))
            return false;
        return p.waitForProcessToFinish (120000) && p.getExitCode() == 0;
    }

    void removeQuarantine (const juce::File& bundle)
    {
        juce::ChildProcess p;
        if (p.start (juce::StringArray { "/usr/bin/xattr", "-dr", "com.apple.quarantine", bundle.getFullPathName() }))
            p.waitForProcessToFinish (20000);
    }

    // Runs a shell script as administrator: macOS shows its own password dialog (osascript, the
    // way installers without a helper do it). False if the password wasn't given.
    bool runAsAdministrator (const juce::String& shellScript)
    {
        const auto literal = [] (const juce::String& t) { return "\"" + t.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\""; };
        const auto apple = "do shell script " + literal (shellScript) + " with prompt "
                           + literal (tr ("OXIde needs your password to update itself in the system plug-in folder."))
                           + " with administrator privileges";
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/osascript", "-e", apple }))
            return false;
        return p.waitForProcessToFinish (300000) && p.getExitCode() == 0;   // time to type the password
    }
   #endif
}

//==============================================================================
Updater::Updater (std::function<juce::PropertiesFile&()> s) : settings (std::move (s))
{
    if (repository().isEmpty())
        setState (State::Disabled, tr ("Updates are not set up in this build (no GitHub repository)."));
    else
        setState (State::Idle, tr ("Version %1.", { currentVersion() }));
}

Updater::~Updater()
{
    cancel = true;
    pool.removeAllJobs (true, 8000);
}

juce::String Updater::currentVersion()  { return OSCAR_VERSION; }
juce::String Updater::repository()      { return juce::String (OSCAR_UPDATE_REPO).trim(); }

bool Updater::getAutoCheck() const      { return settings().getBoolValue ("updateAuto", true); }

void Updater::setAutoCheck (bool b)
{
    settings().setValue ("updateAuto", b);
    settings().saveIfNeeded();
    changes.sendChangeMessage();
}

void Updater::setState (State s, const juce::String& text)
{
    state = s;
    status = text;
    changes.sendChangeMessage();
}

void Updater::openReleasePage() const
{
    const auto page = release.pageUrl.isNotEmpty() ? release.pageUrl
                                                   : "https://github.com/" + repository() + "/releases/latest";
    juce::URL (page).launchInDefaultBrowser();
}

void Updater::skipThisVersion()
{
    settings().setValue ("updateSkip", release.version);
    settings().saveIfNeeded();
    setState (State::UpToDate, tr ("Version %1 skipped. You are on %2.", { release.version, currentVersion() }));
}

//==============================================================================
void Updater::checkIfDue()
{
    if (state != State::Idle || ! getAutoCheck())
        return;
    const auto last = settings().getValue ("updateLastCheck", "0").getLargeIntValue();
    if (juce::Time::currentTimeMillis() - last < (juce::int64) 24 * 3600 * 1000)
        return;
    automatic = true;
    checkNow();
}

void Updater::checkNow()
{
    if (repository().isEmpty() || state == State::Checking || state == State::Installing)
        return;

    setState (State::Checking, tr ("Checking for updates..."));
    settings().setValue ("updateLastCheck", juce::String (juce::Time::currentTimeMillis()));
    settings().saveIfNeeded();

    const auto url = "https://api.github.com/repos/" + repository() + "/releases/latest";
    pool.addJob ([this, weak = juce::WeakReference<Updater> (this), url]
    {
        juce::MemoryBlock mb;
        const auto err = httpGet (url, mb, cancel, 10000);
        std::optional<Release> rel;
        if (err.isEmpty())
            rel = parseLatestRelease (mb.toString(), assetName);

        juce::MessageManager::callAsync ([weak, err, rel]
        {
            auto* u = weak.get();
            if (u == nullptr)
                return;
            const bool wasAutomatic = std::exchange (u->automatic, false);

            if (err.isNotEmpty() || ! rel)
            {
                u->setState (wasAutomatic ? State::Idle : State::Failed,
                             tr ("Couldn't check for updates: %1", { err.isNotEmpty() ? err : tr ("unexpected answer from GitHub.") }));
                return;
            }

            u->release = *rel;
            const bool skipped = wasAutomatic && u->settings().getValue ("updateSkip") == rel->version;
            if (isNewer (rel->version, currentVersion()) && ! skipped)
                u->setState (State::Available, tr ("Version %1 is available (you have %2).", { rel->version, currentVersion() }));
            else
                u->setState (State::UpToDate, tr ("You have the latest version (%1).", { currentVersion() }));
        });
    });
}

void Updater::install()
{
    if (state != State::Available)
        return;

   #if ! JUCE_MAC
    openReleasePage();   // Windows: the plug-in folder needs administrator rights
   #else
    setState (State::Installing, tr ("Downloading version %1...", { release.version }));
    pool.addJob ([this, weak = juce::WeakReference<Updater> (this), rel = release]
    {
        juce::MemoryBlock zip, sha;
        juce::String err = httpGet (rel.zipUrl, zip, cancel, 30000);
        if (err.isEmpty())
        {
            if (rel.shaUrl.isEmpty())
                err = tr ("This release has no checksum file, so it was not installed.");
            else
                err = httpGet (rel.shaUrl, sha, cancel, 15000);
        }
        if (err.isEmpty())
        {
            const auto want = expectedSha (sha.toString());
            const auto got = juce::SHA256 (zip).toHexString();
            if (want.isEmpty() || got != want)
                err = tr ("The download is damaged (checksum mismatch). Nothing was installed.");
        }

        juce::StringArray installed;
        if (err.isEmpty())
        {
            const auto tmp = juce::File::createTempFile (".zip");
            if (! tmp.replaceWithData (zip.getData(), zip.getSize()))
            {
                err = tr ("Couldn't write the download to disk.");
            }
            else
            {
                const auto targets = installedBundles();
                if (targets.isEmpty())
                {
                    err = tr ("OXIde wasn't found in the usual plug-in folders: install this version by hand.");
                }
                else
                {
                    const auto r = installBundles (tmp, targets, rel.version, dittoExtract, installed, runAsAdministrator);
                    if (r.failed())
                        err = r.getErrorMessage();
                    for (const auto& path : installed)
                        removeQuarantine (juce::File (path));
                }
            }
            tmp.deleteFile();
        }

        juce::MessageManager::callAsync ([weak, err, installed, rel]
        {
            auto* u = weak.get();
            if (u == nullptr)
                return;
            if (! installed.isEmpty())
                u->setState (State::Installed, tr ("Version %1 installed (%2 items). Quit and reopen your DAW (or the app) to use it.",
                                                   { rel.version, juce::String (installed.size()) })
                                               + (err.isNotEmpty() ? "\n" + err : juce::String()));
            else
                u->setState (State::Failed, err.isNotEmpty() ? err : tr ("Nothing was installed."));
        });
    });
   #endif
}

//==============================================================================
bool Updater::isNewer (const juce::String& candidate, const juce::String& current)
{
    return parts (candidate) > parts (current);
}

std::optional<Updater::Release> Updater::parseLatestRelease (const juce::String& json, const juce::String& asset)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject())
        return std::nullopt;
    if ((bool) v.getProperty ("draft", false) || (bool) v.getProperty ("prerelease", false))
        return std::nullopt;

    Release r;
    r.version = normalise (v.getProperty ("tag_name", {}).toString());
    r.title   = v.getProperty ("name", {}).toString();
    r.notes   = v.getProperty ("body", {}).toString().replace ("\r\n", "\n");
    r.pageUrl = v.getProperty ("html_url", {}).toString();
    if (r.version.isEmpty() || parts (r.version) == std::array<int, 3> { 0, 0, 0 })
        return std::nullopt;

    if (const auto* assets = v.getProperty ("assets", {}).getArray())
        for (const auto& a : *assets)
        {
            const auto name = a.getProperty ("name", {}).toString();
            const auto url = a.getProperty ("browser_download_url", {}).toString();
            if (name == asset)                 r.zipUrl = url;
            if (name == asset + ".sha256")     r.shaUrl = url;
        }

    if (! r.pageUrl.startsWith ("https://github.com/"))
        r.pageUrl = {};
    for (auto* u : { &r.zipUrl, &r.shaUrl })
        if (! u->startsWith ("https://"))
            u->clear();
    if (r.zipUrl.isEmpty())
        return std::nullopt;
    return r;
}

juce::String Updater::expectedSha (const juce::String& text)
{
    // "shasum -a 256" format: "<64 hex>  <file name>"
    const auto token = text.trim().upToFirstOccurrenceOf (" ", false, false).toLowerCase();
    return token.length() == 64 && token.containsOnly ("0123456789abcdef") ? token : juce::String();
}

juce::String Updater::notesForLanguage (const juce::String& notes, const juce::String& language)
{
    const auto lines = juce::StringArray::fromLines (notes);
    int en = -1, fr = -1;
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto l = lines[i].trim();
        if (! l.startsWith ("## "))
            continue;
        const auto title = l.substring (3).trim().toLowerCase();
        if (title == "english") en = i;
        else if (title == juce::String::fromUTF8 ("fran\xc3\xa7" "ais") || title == "francais") fr = i;
    }
    if (en < 0 || fr < 0)
        return notes;

    const int start = (language == "fr" ? fr : en) + 1;
    const int other = language == "fr" ? en : fr;
    const int end = other > start ? other : lines.size();
    juce::StringArray section;
    for (int i = start; i < end; ++i)
        section.add (lines[i]);
    return section.joinIntoString ("\n").trim();
}

juce::Array<juce::File> Updater::installedBundles()
{
    juce::Array<juce::File> r;
    auto add = [&r] (const juce::File& f) { if (f.isDirectory() && ! r.contains (f)) r.add (f); };

    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    for (const auto* name : { productName, legacyName })
    {
        const juce::String n (name);
        for (const auto& base : { home, juce::File ("/") })
        {
            add (base.getChildFile ("Library/Audio/Plug-Ins/Components/" + n + ".component"));
            add (base.getChildFile ("Library/Audio/Plug-Ins/VST3/" + n + ".vst3"));
        }
        add (home.getChildFile ("Applications/" + n + ".app"));
        add (juce::File ("/Applications/" + n + ".app"));
    }

    // the running copy, wherever it is
    const auto self = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    if (self.hasFileExtension ("component;vst3;app")
        && (self.getFileNameWithoutExtension() == productName || self.getFileNameWithoutExtension() == legacyName))
        add (self);
    return r;
}

juce::Array<juce::File> Updater::outdatedCopies (const juce::Array<juce::File>& installed)
{
    juce::Array<juce::File> r;
    for (const auto& old : installed)
        if (old.getFileNameWithoutExtension() == legacyName)
            for (const auto& f : installed)
                if (f.getFileNameWithoutExtension() == productName && f.getFileExtension() == old.getFileExtension())
                {
                    r.addIfNotAlreadyThere (old);
                    break;
                }
    return r;
}

juce::Array<juce::File> Updater::outdatedCopies()
{
    return outdatedCopies (installedBundles());
}

juce::String Updater::shellQuote (const juce::String& s)
{
    return "'" + s.replace ("'", "'\\''") + "'";
}

juce::Result Updater::installBundles (const juce::File& zip, const juce::Array<juce::File>& targets,
                                      const juce::String& expectedVersion, const Extractor& extract,
                                      juce::StringArray& installed, const Elevator& elevate,
                                      const std::function<bool (const juce::File&)>& writable)
{
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    juce::StringArray problems;

    juce::Array<juce::File> done;   // bundles already installed by this call

    // bundles waiting for the administrator step: the checked new bundle, where it goes, the
    // copies with the old name to remove, and the temporary folder to clear afterwards
    struct Pending { juce::File fresh, dest; juce::Array<juce::File> remove; juce::File staging; };
    std::vector<Pending> pending;

    for (const auto& target : targets)
    {
        // From 0.5 the bundles are named OXIde: a copy with the old name is replaced by the new
        // one next to it, then removed.
        const auto parent = target.getParentDirectory();
        const auto dest = parent.getChildFile (juce::String (productName) + target.getFileExtension());
        const auto name = dest.getFileName();
        const auto removeRenamed = [&]
        {
            if (target == dest || ! target.exists())
                return;
            const auto gone = parent.getChildFile ("." + target.getFileName() + ".old-" + stamp);
            if (target.moveFileTo (gone))
                gone.deleteRecursively();
            else
                problems.add (tr ("%1: the copy with the old name couldn't be removed. Delete it by hand.", { target.getFullPathName() }));
        };

        if (done.contains (dest))
        {
            removeRenamed();
            continue;
        }
        bool alreadyPending = false;
        for (auto& p : pending)
            if (p.dest == dest)
            {
                if (target != dest)
                    p.remove.addIfNotAlreadyThere (target);
                alreadyPending = true;
            }
        if (alreadyPending)
            continue;

        // unpacked next to the bundle when the folder can be written in, otherwise in a
        // temporary folder for the administrator step
        bool direct = writable ? writable (parent) : true;
        auto staging = parent.getChildFile (".oscar-update-" + stamp);
        if (direct)
        {
            staging.deleteRecursively();
            direct = staging.createDirectory().wasOk();
        }
        if (! direct)
        {
            if (! elevate)
            {
                problems.add (tr ("%1: no permission to write in %2 (install it by hand).", { name, parent.getFullPathName() }));
                continue;
            }
            staging = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("oxide-update-" + stamp).getChildFile (target.getFileExtension().substring (1));
            staging.deleteRecursively();
            if (! staging.createDirectory())
            {
                problems.add (tr ("%1: the downloaded file could not be unpacked.", { name }));
                continue;
            }
        }

        auto cleanup = [&staging] { staging.deleteRecursively(); };

        if (! extract (zip, staging))
        {
            problems.add (tr ("%1: the downloaded file could not be unpacked.", { name }));
            cleanup();
            continue;
        }

        auto fresh = staging.getChildFile (name);
        if (! fresh.isDirectory())
            fresh = staging.getChildFile (target.getFileName());   // a zip with the old names
        const auto plist = fresh.getChildFile ("Contents/Info.plist").loadFileAsString();
        if (! fresh.isDirectory() || ! plist.contains (kBundleId) || ! plist.contains (normalise (expectedVersion)))
        {
            problems.add (tr ("%1: not found in the release, or not the expected version. Left unchanged.", { name }));
            cleanup();
            continue;
        }

        if (! direct)
        {
            Pending p { fresh, dest, {}, staging };
            if (target != dest)
                p.remove.add (target);
            pending.push_back (p);
            continue;
        }

        const bool hadDest = dest.exists();
        const auto old = parent.getChildFile ("." + name + ".old-" + stamp);
        if (hadDest && ! dest.moveFileTo (old))
        {
            problems.add (tr ("%1: couldn't be replaced (is it in use or protected?). Left unchanged.", { name }));
            cleanup();
            continue;
        }
        if (! fresh.moveFileTo (dest))
        {
            if (hadDest)
                old.moveFileTo (dest);   // put the previous version back
            problems.add (tr ("%1: couldn't be replaced. The previous version was kept.", { name }));
            cleanup();
            continue;
        }

        old.deleteRecursively();
        cleanup();
        installed.add (dest.getFullPathName());
        done.add (dest);
        removeRenamed();
    }

    // The administrator step: one script for every protected bundle, so the password is asked
    // once. Per bundle: the old one aside, the new one moved in, then the old one deleted (or put
    // back if the copy failed), the copies with the old name removed, the quarantine flag cleared.
    if (! pending.empty())
    {
        juce::StringArray script;
        for (const auto& p : pending)
        {
            const auto q = [] (const juce::File& f) { return shellQuote (f.getFullPathName()); };
            const auto old = p.dest.getParentDirectory().getChildFile ("." + p.dest.getFileName() + ".old-" + stamp);
            juce::String removeOld = "true";
            for (const auto& r : p.remove)
                removeOld << "; /bin/rm -rf " << q (r);
            script.add ("if [ -e " + q (p.dest) + " ]; then /bin/mv " + q (p.dest) + " " + q (old) + "; fi; "
                        "if [ ! -e " + q (p.dest) + " ] && /bin/mv " + q (p.fresh) + " " + q (p.dest) + "; then "
                        "/bin/rm -rf " + q (old) + "; " + removeOld + "; /usr/bin/xattr -dr com.apple.quarantine " + q (p.dest) + " 2>/dev/null; "
                        "else [ ! -e " + q (old) + " ] || { /bin/rm -rf " + q (p.dest) + "; /bin/mv " + q (old) + " " + q (p.dest) + "; }; fi");
        }
        script.add ("true");
        const bool ran = elevate (script.joinIntoString ("; "));

        for (const auto& p : pending)
        {
            const auto plist = p.dest.getChildFile ("Contents/Info.plist").loadFileAsString();
            if (ran && plist.contains (kBundleId) && plist.contains (normalise (expectedVersion)))
            {
                installed.add (p.dest.getFullPathName());
                done.add (p.dest);
            }
            else if (! ran)
            {
                problems.add (tr ("%1: the administrator password was not given. Left unchanged.", { p.dest.getFileName() }));
            }
            else
            {
                problems.add (tr ("%1: couldn't be installed in %2, even as administrator. Left unchanged.",
                                  { p.dest.getFileName(), p.dest.getParentDirectory().getFullPathName() }));
            }
            p.staging.getParentDirectory().deleteRecursively();
        }
    }

    return problems.isEmpty() ? juce::Result::ok() : juce::Result::fail (problems.joinIntoString ("\n"));
}
