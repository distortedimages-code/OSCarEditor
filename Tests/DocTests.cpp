// Tests for BankDocument: sync tracking, undo, names, files and host state.

#include <JuceHeader.h>

#include "../Source/BankDocument.h"
#include "../Source/core/FactoryBank.h"

using namespace oscar;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static VoiceBank factoryBank()
{
    VoiceBank b;
    for (int i = 0; i < kNumVoices; ++i)
        for (int k = 0; k < kVoiceSize; ++k)
            b.voices[(size_t) i][(size_t) k] = factory::kVoiceBank[i * kVoiceSize + k];
    return b;
}

static DumpContents dumpOf (const VoiceBank& b)
{
    DumpContents d;
    d.voices = b;
    return d;
}

static void testSyncAndUndo()
{
    BankDocument doc;
    CHECK (doc.getBank() == factoryBank());
    CHECK (doc.getVoiceSync() == BankDocument::Sync::Unknown);
    CHECK (doc.getName (0) == "GlassandoLead");
    CHECK (! doc.canUndo());

    const auto report = doc.receiveDump (dumpOf (factoryBank()));
    CHECK (report.contains ("Voices received"));
    CHECK (doc.getVoiceSync() == BankDocument::Sync::InSync);
    CHECK (doc.getName (35) == "Snare");

    const auto* freq = findParam ("filterFreq");
    const int before = getParam (doc.getVoice (14), *freq);
    doc.setParam (14, *freq, (before + 10) % 255);
    doc.setParam (14, *freq, (before + 20) % 255);   // same control: coalesced
    CHECK (doc.getVoiceSync() == BankDocument::Sync::Edited);
    CHECK (doc.slotsDifferingFromOscar().size() == 1 && doc.slotsDifferingFromOscar()[0] == 14);
    CHECK (doc.isSlotEdited (14) && ! doc.isSlotEdited (13));

    doc.undo();   // one step undoes the whole "drag"
    CHECK (getParam (doc.getVoice (14), *freq) == before);
    CHECK (doc.getVoiceSync() == BankDocument::Sync::InSync);
    doc.redo();
    CHECK (getParam (doc.getVoice (14), *freq) == (before + 20) % 255);

    doc.revertSlot (14);
    CHECK (doc.getVoiceSync() == BankDocument::Sync::InSync);

    // writing an out-of-range value is refused and records nothing
    const bool couldUndo = doc.canUndo();
    doc.setParam (3, *freq, 255);
    CHECK (doc.getVoiceSync() == BankDocument::Sync::InSync);
    CHECK (doc.canUndo() == couldUndo);

    // after a send, the OSCar is assumed to hold the bank
    doc.setParam (0, *freq, 3);
    doc.markVoicesSent();
    CHECK (doc.getVoiceSync() == BankDocument::Sync::InSync);

    // a dump identical to what was sent verifies the transfer
    auto expected = doc.getBank();
    const auto verify = doc.receiveDump (dumpOf (expected));
    CHECK (verify.contains ("identical"));

    // a dump that differs is reported, slot by slot
    expected.voices[5][0] = (std::uint8_t) ((expected.voices[5][0] + 1) & 0x0F);
    const auto diff = doc.receiveDump (dumpOf (expected));
    CHECK (diff.contains ("1 voice(s) differ") && diff.contains ("-6"));
}

static void testNamesFollowVoices()
{
    BankDocument doc;
    doc.receiveDump (dumpOf (factoryBank()));
    doc.rename (2, "My Lead");

    auto moved = factoryBank();
    std::swap (moved.voices[2], moved.voices[20]);
    doc.receiveDump (dumpOf (moved));
    CHECK (doc.getName (20) == "My Lead");
    CHECK (doc.getName (2) == juce::String (factory::kVoiceNames[20]));

    // an unknown voice gets no name
    auto changed = moved;
    changed.voices[7][3] = (std::uint8_t) ((changed.voices[7][3] + 3) & 0x0F);
    doc.receiveDump (dumpOf (changed));
    CHECK (doc.getName (7).isEmpty());
    CHECK (doc.getDisplayName (7) == "(unnamed)");

    // unsent edits replaced by a dump are mentioned and undoable
    doc.setParam (1, *findParam ("detune"), 10);
    const auto r = doc.receiveDump (dumpOf (changed));
    CHECK (r.contains ("Unsent edits"));
    doc.undo();
    CHECK (getParam (doc.getVoice (1), *findParam ("detune")) == 10);
}

static void testClipboardAndSwap()
{
    BankDocument doc;
    const auto v3 = doc.getVoice (3);
    const auto n3 = doc.getName (3);
    doc.copySlot (3);
    doc.pasteSlot (30);
    CHECK (doc.getVoice (30) == v3 && doc.getName (30) == n3);
    doc.undo();
    CHECK (doc.getVoice (30) == factoryBank().voices[30]);

    doc.swapSlots (0, 35);
    CHECK (doc.getVoice (35) == factoryBank().voices[0] && doc.getName (35) == "GlassandoLead");
    doc.undo();
    CHECK (doc.getBank() == factoryBank());
}

static void testFiles()
{
    auto dir = juce::File::createTempFile ("oscartest");
    dir.createDirectory();

    BankDocument doc;
    doc.rename (4, "Renamed");
    doc.setParam (4, *findParam ("resonance"), 77);

    // .oscbank round trip (names + data)
    const auto bankFile = dir.getChildFile ("a.oscbank");
    CHECK (doc.saveBankFile (bankFile).wasOk());
    BankDocument other;
    juce::String report;
    CHECK (other.loadFile (bankFile, report).wasOk());
    CHECK (other.getBank() == doc.getBank());
    CHECK (other.getName (4) == "Renamed");
    CHECK (other.getVoiceSync() == BankDocument::Sync::Unknown); // loading a file says nothing about the OSCar

    // .syx round trip
    const auto syx = dir.getChildFile ("a.syx");
    CHECK (doc.exportSyx (syx).wasOk());
    CHECK (syx.getSize() == 2308);
    BankDocument third;
    CHECK (third.loadFile (syx, report).wasOk());
    CHECK (third.getBank() == doc.getBank());

    // single voice
    const auto vf = dir.getChildFile ("v.oscvoice");
    CHECK (doc.exportVoice (4, vf).wasOk());
    BankDocument fourth;
    CHECK (fourth.importVoice (9, vf).wasOk());
    CHECK (fourth.getVoice (9) == doc.getVoice (4) && fourth.getName (9) == "Renamed");

    // damaged files are refused and change nothing
    const auto snapshot = fourth.getBank();
    auto bad = dir.getChildFile ("bad.syx");
    juce::MemoryBlock mb;
    syx.loadFileAsData (mb);
    static_cast<std::uint8_t*> (mb.getData())[500] = 0x40;
    bad.replaceWithData (mb.getData(), mb.getSize());
    CHECK (fourth.loadFile (bad, report).failed());
    CHECK (fourth.getBank() == snapshot);

    auto badBank = dir.getChildFile ("bad.oscbank");
    badBank.replaceWithText (bankFile.loadFileAsString().replace ("voices=\"0", "voices=\"Z"));
    CHECK (fourth.loadFile (badBank, report).failed());
    CHECK (fourth.getBank() == snapshot);

    auto badVoice = dir.getChildFile ("bad.oscvoice");
    badVoice.replaceWithText ("<OSCarVoice name=\"x\" data=\"0102\"/>");
    CHECK (fourth.importVoice (0, badVoice).failed());
    CHECK (fourth.getBank() == snapshot);

    // the codemechanic archive: voices load, damaged waveforms are skipped
    auto archive = juce::File::getCurrentWorkingDirectory().getChildFile ("Tests/factory_codemechanic.syx");
    if (archive.existsAsFile())
    {
        BankDocument fifth;
        CHECK (fifth.loadFile (archive, report).wasOk());
        CHECK (report.contains ("damaged"));
        CHECK (! fifth.getWaves().has_value());
        CHECK (fifth.getBank() == factoryBank());
    }
    else
    {
        std::printf ("note: run from the project folder to include the archive test\n");
    }

    dir.deleteRecursively();
}

static void testHostState()
{
    BankDocument doc;
    doc.receiveDump (dumpOf (factoryBank()));
    doc.rename (10, "State test");
    doc.setParam (10, *findParam ("wave1"), 2);

    const auto tree = doc.toValueTree();
    const auto xml = tree.toXmlString();

    BankDocument restored;
    restored.fromValueTree (juce::ValueTree::fromXml (xml));
    CHECK (restored.getBank() == doc.getBank());
    CHECK (restored.getName (10) == "State test");
    CHECK (restored.getVoiceSync() == BankDocument::Sync::Unknown); // never assume after a reload

    // a broken state is ignored
    BankDocument untouched;
    untouched.fromValueTree (juce::ValueTree ("Nope"));
    CHECK (untouched.getBank() == factoryBank());
}

static void testHex()
{
    std::vector<std::uint8_t> out;
    CHECK (BankDocument::hexToBytes ("0A0f", out) && out.size() == 2 && out[0] == 0x0A && out[1] == 0x0F);
    CHECK (! BankDocument::hexToBytes ("0A0", out));
    CHECK (! BankDocument::hexToBytes ("0G", out));
    const std::uint8_t d[] = { 0x00, 0x0F, 0xAB };
    CHECK (BankDocument::bytesToHex (d, 3) == "000FAB");
}

static void testWaves()
{
    BankDocument doc;
    doc.setHarmonic (0, 0, 100);       // no waves loaded: ignored
    CHECK (! doc.getWaves().has_value());

    DumpContents d;
    d.waves = WaveBank {};
    doc.receiveDump (d);
    CHECK (doc.getWaves().has_value() && ! doc.areWavesEdited());
    doc.setHarmonic (2, 5, 200);
    CHECK (doc.areWavesEdited() && doc.getWaves()->getHarmonic (2, 5) == 200);
    doc.markWavesSent();
    CHECK (! doc.areWavesEdited());
}

// The exact bytes MidiLink puts on the wire, and what it hands back from the input.
static void testMidiWrapping()
{
    const auto msg = buildVoiceMessage (factoryBank());
    const auto out = juce::MidiMessage::createSysExMessage (msg.data() + 1, (int) msg.size() - 2);
    CHECK (out.isSysEx());
    CHECK (out.getRawDataSize() == (int) msg.size());
    CHECK (std::equal (msg.begin(), msg.end(), out.getRawData()));

    const juce::MidiMessage in (msg.data(), (int) msg.size());
    CHECK (in.isSysEx() && in.getRawDataSize() == (int) msg.size());
    std::vector<std::uint8_t> back (in.getRawData(), in.getRawData() + in.getRawDataSize());
    DumpContents d;
    std::string err;
    CHECK (parseStream (back, d, err) && d.voices && *d.voices == factoryBank());

    // programme change numbering: slot index == MIDI programme number
    const auto pc = juce::MidiMessage::programChange (1, 12);
    CHECK (pc.getRawData()[0] == 0xC0 && pc.getRawData()[1] == 12);
    CHECK (slotLabel (12) == "1");
}

int main()
{
    juce::MessageManager::getInstance();

    testSyncAndUndo();
    testNamesFollowVoices();
    testClipboardAndSwap();
    testFiles();
    testHostState();
    testHex();
    testWaves();
    testMidiWrapping();

    juce::MessageManager::deleteInstance();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
