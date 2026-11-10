// Tests for the update checker's offline parts: version compare, GitHub answer parsing,
// checksum file, and the in-place replacement of installed bundles.

#include <JuceHeader.h>

#include "../Source/Updater.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static void testVersions()
{
    CHECK (Updater::isNewer ("v0.3.0", "0.2.0"));
    CHECK (Updater::isNewer ("0.2.1", "0.2.0"));
    CHECK (Updater::isNewer ("1.0", "0.9.9"));
    CHECK (Updater::isNewer ("v0.10.0", "0.9.0"));      // numeric, not alphabetical
    CHECK (! Updater::isNewer ("0.2.0", "0.2.0"));
    CHECK (! Updater::isNewer ("v0.1.9", "0.2.0"));
    CHECK (! Updater::isNewer ("0.2.0-beta", "0.2.0"));
    CHECK (! Updater::isNewer ("garbage", "0.2.0"));
}

static juce::String releaseJson (const juce::String& tag, bool draft, bool withSha)
{
    // the old name first (for 0.4 updaters), then the one this updater wants
    juce::String assets;
    for (const juce::String name : { "OSCar-Editor-mac.zip", "OXIde-mac.zip" })
    {
        assets << (assets.isEmpty() ? "" : ", ") << "{ \"name\": \"" << name << "\", \"browser_download_url\": \"https://github.com/o/r/releases/download/" << tag << "/" << name << "\" }";
        if (withSha)
            assets << ", { \"name\": \"" << name << ".sha256\", \"browser_download_url\": \"https://github.com/o/r/releases/download/" << tag << "/" << name << ".sha256\" }";
    }
    return "{ \"tag_name\": \"" + tag + "\", \"name\": \"Big update\", \"draft\": " + juce::String (draft ? "true" : "false")
         + ", \"prerelease\": false, \"html_url\": \"https://github.com/o/r/releases/tag/" + tag + "\", "
           "\"body\": \"- New look\\r\\n- Generator\", \"assets\": [ " + assets + " ] }";
}

static void testParsing()
{
    auto r = Updater::parseLatestRelease (releaseJson ("v0.3.0", false, true), Updater::assetName);
    CHECK (r.has_value());
    if (r)
    {
        CHECK (r->version == "0.3.0");
        CHECK (r->title == "Big update");
        CHECK (r->notes == "- New look\n- Generator");
        CHECK (r->zipUrl.endsWith ("/OXIde-mac.zip"));
        CHECK (r->shaUrl.endsWith ("/OXIde-mac.zip.sha256"));
        CHECK (r->pageUrl.startsWith ("https://github.com/"));
    }
    CHECK (! Updater::parseLatestRelease (releaseJson ("v0.3.0", true, true), Updater::assetName).has_value());   // draft
    auto noSha = Updater::parseLatestRelease (releaseJson ("v0.3.0", false, false), Updater::assetName);
    CHECK (noSha.has_value() && noSha->shaUrl.isEmpty());                                                        // install refuses later
    CHECK (! Updater::parseLatestRelease ("not json", Updater::assetName).has_value());
    CHECK (! Updater::parseLatestRelease ("{ \"message\": \"Not Found\" }", Updater::assetName).has_value());

    const juce::String hex = "3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b";
    CHECK (Updater::expectedSha (hex + "  OXIde-mac.zip\n") == hex);
    CHECK (Updater::expectedSha (hex.toUpperCase()) == hex);
    CHECK (Updater::expectedSha ("nope") == "");
}

// A fake installed bundle and a fake release zip.
static void makeBundle (const juce::File& bundle, const juce::String& version, const juce::String& payload)
{
    bundle.getChildFile ("Contents/MacOS").createDirectory();
    bundle.getChildFile ("Contents/Info.plist").replaceWithText (
        "<plist><dict><key>CFBundleIdentifier</key><string>com.oscartools.oscareditor</string>"
        "<key>CFBundleShortVersionString</key><string>" + version + "</string></dict></plist>");
    bundle.getChildFile ("Contents/MacOS/OXIde").replaceWithText (payload);
}

static juce::File makeZip (const juce::File& dir, const juce::String& version, juce::StringArray bundleNames)
{
    const auto src = dir.getChildFile ("zip-src");
    src.deleteRecursively();
    juce::ZipFile::Builder builder;
    for (const auto& name : bundleNames)
    {
        makeBundle (src.getChildFile (name), version, "new " + version);
        for (const auto& f : src.getChildFile (name).findChildFiles (juce::File::findFiles, true))
            builder.addFile (f, 9, f.getRelativePathFrom (src).replaceCharacter ('\\', '/'));
    }
    const auto zip = dir.getChildFile ("release.zip");
    zip.deleteFile();
    juce::FileOutputStream os (zip);
    builder.writeToStream (os, nullptr);
    return zip;
}

static bool unzip (const juce::File& zip, const juce::File& dest)
{
    juce::ZipFile z (zip);
    return z.getNumEntries() > 0 && z.uncompressTo (dest).wasOk();
}

static void testInstall()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("oscar-updater-test");
    dir.deleteRecursively();
    const auto components = dir.getChildFile ("Components"), vst3 = dir.getChildFile ("VST3");
    const auto au = components.getChildFile ("OXIde.component"), vst = vst3.getChildFile ("OXIde.vst3");
    makeBundle (au, "0.2.0", "old");
    makeBundle (vst, "0.2.0", "old");

    // the normal case: both replaced, nothing left behind
    auto zip = makeZip (dir, "0.3.0", { "OXIde.component", "OXIde.vst3", "OXIde.app" });
    juce::StringArray installed;
    auto r = Updater::installBundles (zip, { au, vst }, "v0.3.0", unzip, installed);
    CHECK (r.wasOk());
    CHECK (installed.size() == 2);
    CHECK (au.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.3.0");
    CHECK (vst.getChildFile ("Contents/Info.plist").loadFileAsString().contains ("0.3.0"));
    CHECK (components.findChildFiles (juce::File::findFilesAndDirectories, false, "*").size() == 1);   // no staging or .old left
    CHECK (components.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false, "*").size() == 1);

    // a zip with the wrong version is refused and the installed bundle is kept
    zip = makeZip (dir, "0.2.5", { "OXIde.component" });
    installed.clear();
    r = Updater::installBundles (zip, { au }, "0.4.0", unzip, installed);
    CHECK (r.failed() && installed.isEmpty());
    CHECK (au.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.3.0");

    // a format missing from the release: that one is left alone, the others are updated
    zip = makeZip (dir, "0.4.0", { "OXIde.component" });
    installed.clear();
    r = Updater::installBundles (zip, { au, vst }, "0.4.0", unzip, installed);
    CHECK (r.failed() && installed.size() == 1);
    CHECK (au.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.4.0");
    CHECK (vst.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.3.0");

    // an unreadable download changes nothing
    const auto bad = dir.getChildFile ("bad.zip");
    bad.replaceWithText ("this is not a zip");
    installed.clear();
    r = Updater::installBundles (bad, { au }, "0.5.0", unzip, installed);
    CHECK (r.failed() && installed.isEmpty());
    CHECK (au.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.4.0");
    CHECK (components.findChildFiles (juce::File::findFilesAndDirectories, false, "*").size() == 1);

    dir.deleteRecursively();
}

// 0.4 and older installed "OSCar Editor.*": the update puts OXIde.* next to it and removes it.
static void testRename()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("oscar-updater-rename");
    dir.deleteRecursively();
    const auto components = dir.getChildFile ("Components"), vst3 = dir.getChildFile ("VST3");
    const auto oldAu = components.getChildFile ("OSCar Editor.component"), oldVst = vst3.getChildFile ("OSCar Editor.vst3");
    const auto newAu = components.getChildFile ("OXIde.component"), newVst = vst3.getChildFile ("OXIde.vst3");
    makeBundle (oldAu, "0.4.2", "old");
    makeBundle (oldVst, "0.4.2", "old");
    makeBundle (newVst, "0.5.0", "dmg");   // installed by hand next to the old one: a duplicate

    CHECK (Updater::outdatedCopies ({ oldAu, oldVst, newVst }) == juce::Array<juce::File> { oldVst });   // the AU has no OXIde twin yet

    const auto zip = makeZip (dir, "0.5.1", { "OXIde.component", "OXIde.vst3", "OXIde.app" });
    juce::StringArray installed;
    const auto r = Updater::installBundles (zip, { newVst, oldAu, oldVst }, "0.5.1", unzip, installed);
    CHECK (r.wasOk());
    CHECK (installed.size() == 2);
    CHECK (newAu.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.5.1");
    CHECK (newVst.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.5.1");
    CHECK (! oldAu.exists() && ! oldVst.exists());
    CHECK (components.findChildFiles (juce::File::findFilesAndDirectories, false, "*").size() == 1);
    CHECK (vst3.findChildFiles (juce::File::findFilesAndDirectories, false, "*").size() == 1);
    CHECK (Updater::outdatedCopies ({ newAu, newVst }).isEmpty());

    dir.deleteRecursively();
}

// A folder the plug-in can't write in (the AU folder when it was installed with the password):
// the bundle is checked in a temporary folder, then swapped by one script run as administrator.
static void testElevated()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("oscar-updater-elevated");
    const auto components = dir.getChildFile ("Components"), vst3 = dir.getChildFile ("VST3");
    const auto au = components.getChildFile ("OXIde.component"), oldAu = components.getChildFile ("OSCar Editor.component");
    const auto vst = vst3.getChildFile ("OXIde.vst3");
    const auto protectedFolder = [components] (const juce::File& f) { return f != components; };
    const auto shell = [] (const juce::String& script)
    {
        juce::ChildProcess p;
        return p.start (juce::StringArray { "/bin/sh", "-c", script }) && p.waitForProcessToFinish (20000) && p.getExitCode() == 0;
    };
    const auto reset = [&]
    {
        dir.deleteRecursively();
        makeBundle (au, "0.5.0", "old");
        makeBundle (oldAu, "0.4.2", "older");   // a copy with the old name in the same folder
        makeBundle (vst, "0.5.0", "old");
        return makeZip (dir, "0.5.1", { "OXIde.component", "OXIde.vst3", "OXIde.app" });
    };
    const auto entries = [] (const juce::File& f) { return f.findChildFiles (juce::File::findFilesAndDirectories, false, "*", juce::File::FollowSymlinks::no).size(); };

    // the password given: one script for the protected folder, the other folder done directly
    // (a macOS feature: the script needs a Unix shell, so not run on Windows)
   #if ! JUCE_WINDOWS
    {
        const auto zip = reset();
        int runs = 0;
        juce::String ran;
        juce::StringArray installed;
        const auto r = Updater::installBundles (zip, { au, oldAu, vst }, "0.5.1", unzip, installed,
                                                [&] (const juce::String& script) { ++runs; ran = script; return shell (script); }, protectedFolder);
        CHECK (r.wasOk());
        CHECK (runs == 1);
        CHECK (installed.size() == 2);
        CHECK (au.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.5.1");
        CHECK (vst.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "new 0.5.1");
        CHECK (! oldAu.exists());
        CHECK (entries (components) == 1 && entries (vst3) == 1);   // nothing left behind
        CHECK (ran.contains ("/Components/OXIde.component") && ! ran.contains ("VST3"));
    }
   #endif

    // the password refused: the protected bundle stays as it was, the rest is installed
    {
        const auto zip = reset();
        juce::StringArray installed;
        const auto r = Updater::installBundles (zip, { au, oldAu, vst }, "0.5.1", unzip, installed,
                                                [] (const juce::String&) { return false; }, protectedFolder);
        CHECK (r.failed() && r.getErrorMessage().contains ("OXIde.component"));
        CHECK (installed.size() == 1);
        CHECK (au.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "old");
        CHECK (oldAu.exists());
        CHECK (entries (components) == 2);
    }

    // no way to ask (Windows, tests): reported, left as it was
    {
        const auto zip = reset();
        juce::StringArray installed;
        const auto r = Updater::installBundles (zip, { au, vst }, "0.5.1", unzip, installed, {}, protectedFolder);
        CHECK (r.failed() && installed.size() == 1);
        CHECK (au.getChildFile ("Contents/MacOS/OXIde").loadFileAsString() == "old");
    }

    // paths with spaces and quotes survive the shell
    CHECK (Updater::shellQuote ("/a b/it's") == "'/a b/it'\\''s'");
    dir.deleteRecursively();
}

static void testNotesLanguage()
{
    const auto both = juce::String::fromUTF8 ("## English\n\n**Disk image** to install.\n\n## Fran\xc3\xa7" "ais\n\n**Image disque** pour installer.\n");
    CHECK (Updater::notesForLanguage (both, "en") == "**Disk image** to install.");
    CHECK (Updater::notesForLanguage (both, "fr") == "**Image disque** pour installer.");
    const juce::String plain ("Just one language.\n\n## Details\nMore.");
    CHECK (Updater::notesForLanguage (plain, "fr") == plain);
    const auto reversed = juce::String::fromUTF8 ("## Fran\xc3\xa7" "ais\nUn.\n## English\nOne.");
    CHECK (Updater::notesForLanguage (reversed, "en") == "One.");
    CHECK (Updater::notesForLanguage (reversed, "fr") == "Un.");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testVersions();
    testParsing();
    testInstall();
    testRename();
    testElevated();
    testNotesLanguage();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
