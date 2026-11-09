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
    juce::String assets = "{ \"name\": \"OSCar-Editor-mac.zip\", \"browser_download_url\": \"https://github.com/o/r/releases/download/" + tag + "/OSCar-Editor-mac.zip\" }";
    if (withSha)
        assets << ", { \"name\": \"OSCar-Editor-mac.zip.sha256\", \"browser_download_url\": \"https://github.com/o/r/releases/download/" + tag + "/OSCar-Editor-mac.zip.sha256\" }";
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
        CHECK (r->zipUrl.endsWith ("/OSCar-Editor-mac.zip"));
        CHECK (r->shaUrl.endsWith (".sha256"));
        CHECK (r->pageUrl.startsWith ("https://github.com/"));
    }
    CHECK (! Updater::parseLatestRelease (releaseJson ("v0.3.0", true, true), Updater::assetName).has_value());   // draft
    auto noSha = Updater::parseLatestRelease (releaseJson ("v0.3.0", false, false), Updater::assetName);
    CHECK (noSha.has_value() && noSha->shaUrl.isEmpty());                                                        // install refuses later
    CHECK (! Updater::parseLatestRelease ("not json", Updater::assetName).has_value());
    CHECK (! Updater::parseLatestRelease ("{ \"message\": \"Not Found\" }", Updater::assetName).has_value());

    const juce::String hex = "3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b";
    CHECK (Updater::expectedSha (hex + "  OSCar-Editor-mac.zip\n") == hex);
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
    bundle.getChildFile ("Contents/MacOS/OSCar Editor").replaceWithText (payload);
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
    const auto au = components.getChildFile ("OSCar Editor.component"), vst = vst3.getChildFile ("OSCar Editor.vst3");
    makeBundle (au, "0.2.0", "old");
    makeBundle (vst, "0.2.0", "old");

    // the normal case: both replaced, nothing left behind
    auto zip = makeZip (dir, "0.3.0", { "OSCar Editor.component", "OSCar Editor.vst3", "OSCar Editor.app" });
    juce::StringArray installed;
    auto r = Updater::installBundles (zip, { au, vst }, "v0.3.0", unzip, installed);
    CHECK (r.wasOk());
    CHECK (installed.size() == 2);
    CHECK (au.getChildFile ("Contents/MacOS/OSCar Editor").loadFileAsString() == "new 0.3.0");
    CHECK (vst.getChildFile ("Contents/Info.plist").loadFileAsString().contains ("0.3.0"));
    CHECK (components.findChildFiles (juce::File::findFilesAndDirectories, false, "*").size() == 1);   // no staging or .old left
    CHECK (components.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false, "*").size() == 1);

    // a zip with the wrong version is refused and the installed bundle is kept
    zip = makeZip (dir, "0.2.5", { "OSCar Editor.component" });
    installed.clear();
    r = Updater::installBundles (zip, { au }, "0.4.0", unzip, installed);
    CHECK (r.failed() && installed.isEmpty());
    CHECK (au.getChildFile ("Contents/MacOS/OSCar Editor").loadFileAsString() == "new 0.3.0");

    // a format missing from the release: that one is left alone, the others are updated
    zip = makeZip (dir, "0.4.0", { "OSCar Editor.component" });
    installed.clear();
    r = Updater::installBundles (zip, { au, vst }, "0.4.0", unzip, installed);
    CHECK (r.failed() && installed.size() == 1);
    CHECK (au.getChildFile ("Contents/MacOS/OSCar Editor").loadFileAsString() == "new 0.4.0");
    CHECK (vst.getChildFile ("Contents/MacOS/OSCar Editor").loadFileAsString() == "new 0.3.0");

    // an unreadable download changes nothing
    const auto bad = dir.getChildFile ("bad.zip");
    bad.replaceWithText ("this is not a zip");
    installed.clear();
    r = Updater::installBundles (bad, { au }, "0.5.0", unzip, installed);
    CHECK (r.failed() && installed.isEmpty());
    CHECK (au.getChildFile ("Contents/MacOS/OSCar Editor").loadFileAsString() == "new 0.4.0");
    CHECK (components.findChildFiles (juce::File::findFilesAndDirectories, false, "*").size() == 1);

    dir.deleteRecursively();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testVersions();
    testParsing();
    testInstall();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
