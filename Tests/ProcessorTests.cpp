#include <set>
#include <typeinfo>
// End-to-end tests of the plugin's MIDI behaviour, through a virtual OSCar port:
// live CC edits, voice selection, Tauntek CC dumps, host automation, SysEx transfers, state.

#include <JuceHeader.h>

#include "../Source/PluginEditor.h"
#include "BinaryData.h"
#include "../Source/PluginProcessor.h"
#include "../Source/core/FactoryBank.h"

using namespace oscar;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

namespace
{
    struct Capture
    {
        juce::CriticalSection lock;
        std::vector<juce::MidiMessage> messages;

        void add (const juce::MidiMessage& m) { const juce::ScopedLock sl (lock); messages.push_back (m); }
        std::vector<juce::MidiMessage> take()  { const juce::ScopedLock sl (lock); auto r = messages; messages.clear(); return r; }
    };

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    // Runs the message loop until 'done' is true for everything captured so far
    // (or a generous timeout), then lets stragglers arrive and returns it all.
    // Waiting for results instead of fixed delays keeps the test reliable on slow machines.
    std::vector<juce::MidiMessage> collect (Capture& out, std::function<bool (const std::vector<juce::MidiMessage>&)> done,
                                            int timeoutMs = 5000)
    {
        std::vector<juce::MidiMessage> all;
        const auto end = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        while (juce::Time::getMillisecondCounterHiRes() < end)
        {
            pump (10);
            for (auto& m : out.take()) all.push_back (m);
            if (done (all))
                break;
        }
        pump (60);
        for (auto& m : out.take()) all.push_back (m);
        return all;
    }

    bool waitForLog (OscarProcessor& proc, const juce::String& text, int timeoutMs = 5000)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        while (juce::Time::getMillisecondCounterHiRes() < end)
        {
            pump (10);
            if (proc.getLastMessage().contains (text))
                return true;
        }
        return false;
    }

    std::vector<std::pair<int, int>> ccsOf (const std::vector<juce::MidiMessage>& ms);

    auto ccCountAtLeast (size_t n)
    {
        return [n] (const std::vector<juce::MidiMessage>& ms) { return ccsOf (ms).size() >= n; };
    }

    VoiceBank factoryBank()
    {
        VoiceBank b;
        for (int i = 0; i < kNumVoices; ++i)
            for (int k = 0; k < kVoiceSize; ++k)
                b.voices[(size_t) i][(size_t) k] = factory::kVoiceBank[i * kVoiceSize + k];
        return b;
    }

    std::vector<std::pair<int, int>> ccsOf (const std::vector<juce::MidiMessage>& ms)
    {
        std::vector<std::pair<int, int>> r;
        for (const auto& m : ms)
            if (m.isController())
                r.emplace_back (m.getControllerNumber(), m.getControllerValue());
        return r;
    }

    int countPrograms (const std::vector<juce::MidiMessage>& ms, int program)
    {
        int n = 0;
        for (const auto& m : ms)
            if (m.isProgramChange() && m.getProgramChangeNumber() == program)
                ++n;
        return n;
    }

    void injectVoiceDump (OscarProcessor& proc, const VoiceBank& bank)
    {
        const auto msg = buildVoiceMessage (bank);
        proc.getMidi().injectForTest (juce::MidiMessage (msg.data(), (int) msg.size()));
        pump (60);
    }

    // What the OSCar sends after loading a voice (Tauntek patch CC dump).
    void injectCcDump (OscarProcessor& proc, const VoiceData& v)
    {
        bool arpDone = false;
        for (const auto& p : allParams())
        {
            if (p.cc < 0) continue;
            int value = getParam (v, p);
            if (p.coding == CcCoding::ArpNibble)
            {
                if (arpDone) continue;
                arpDone = true;
                value = v[62] & 0x0F;
            }
            proc.getMidi().injectForTest (juce::MidiMessage::controllerEvent (1, p.cc, dumpCcFor (p.coding, value)));
        }
        pump (250);
    }

    void setHost (OscarProcessor& proc, const juce::String& id, float normalised)
    {
        auto* p = proc.getHostParameters().getParameter (id);
        p->setValueNotifyingHost (normalised);
    }
}

static auto hasProgram (int program)
{
    return [program] (const std::vector<juce::MidiMessage>& ms) { return countPrograms (ms, program) > 0; };
}

static void testLiveEditing (OscarProcessor& proc, Capture& out)
{
    pump (200);
    CHECK (out.take().empty());   // nothing is sent at start-up

    // click a voice: the OSCar is switched to it
    proc.chooseVoice (5);
    auto sent = collect (out, hasProgram (5));
    CHECK (countPrograms (sent, 5) == 1);

    // edit a knob: one CC, value = stored / 2
    proc.editParam (5, *findParam ("filterFreq"), 200);
    auto ccs = ccsOf (collect (out, ccCountAtLeast (1)));
    CHECK (ccs.size() == 1 && ccs[0] == std::make_pair (30, 100));

    // arpeggiator: CC 51 then CC 50 (Function restored)
    proc.editParam (5, *findParam ("arpMode"), 2);
    ccs = ccsOf (collect (out, ccCountAtLeast (2)));
    CHECK (ccs.size() == 2 && ccs[0].first == 51 && ccs[1].first == 50);
    CHECK (! ccs.empty() && firmwareValueFromCc (CcCoding::ArpNibble, ccs[0].second) == (proc.getDocument().getVoice (5)[62] & 0x0F));

    // the OSCar plays another voice and the plugin doesn't follow: no CC may touch it
    proc.setFollowOscar (false);
    proc.getMidi().injectForTest (juce::MidiMessage::programChange (1, 9));
    pump (100);
    CHECK (proc.getSelectedSlot() == 5);
    proc.editParam (5, *findParam ("resonance"), 90);
    pump (150);
    CHECK (ccsOf (out.take()).empty());
    proc.setFollowOscar (true);

    // following the OSCar: its programme change selects the voice in the plugin
    proc.getMidi().injectForTest (juce::MidiMessage::programChange (1, 20));
    pump (100);
    CHECK (proc.getSelectedSlot() == 20);
    pump (600);
    out.take();
}

static void testKnownState (OscarProcessor& proc, Capture& out)
{
    // a SysEx dump from the OSCar: the plugin now knows its memory
    injectVoiceDump (proc, factoryBank());
    pump (100);
    CHECK (proc.getDocument().getVoiceSync() == BankDocument::Sync::InSync);
    out.take();

    // selecting an unedited voice sends nothing but the programme change
    proc.chooseVoice (3);
    auto sent = collect (out, hasProgram (3));
    pump (700);                                   // past the post-selection sync
    for (auto& m : out.take()) sent.push_back (m);
    CHECK (countPrograms (sent, 3) == 1 && ccsOf (sent).empty());

    // an edit sends exactly that parameter; undo sends it back
    const auto* q = findParam ("resonance");
    const int before = getParam (proc.getDocument().getVoice (3), *q);
    proc.editParam (3, *q, 60);
    auto ccs = ccsOf (collect (out, ccCountAtLeast (1)));
    CHECK (ccs.size() == 1 && ccs[0] == std::make_pair (32, 30));
    proc.getDocument().undo();
    ccs = ccsOf (collect (out, ccCountAtLeast (1)));
    CHECK (ccs.size() == 1 && ccs[0] == std::make_pair (32, before >> 1));

    // a voice edited while live CC was off is pushed when it gets selected
    proc.setLiveCc (false);
    proc.editParam (10, *findParam ("filterFreq"), 10);
    proc.editParam (10, *findParam ("tuneSemi"), 9);
    proc.setLiveCc (true);
    pump (150);
    CHECK (ccsOf (out.take()).empty());
    proc.chooseVoice (10);
    sent = collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return countPrograms (ms, 10) > 0 && ccsOf (ms).size() >= 2; });
    CHECK (countPrograms (sent, 10) == 1);
    ccs = ccsOf (sent);
    CHECK (ccs.size() == 2);
    bool freq = false, semi = false;
    for (auto& c : ccs)
    {
        if (c == std::make_pair (30, 5)) freq = true;
        if (c.first == 20 && firmwareValueFromCc (CcCoding::Semitone, c.second) == 9) semi = true;
    }
    CHECK (freq && semi);
}

static void testCcDump (OscarProcessor& proc, Capture& out)
{
    proc.chooseVoice (6);
    pump (50);
    const auto memory = *proc.getDocument().getOscarVoice (6);
    injectCcDump (proc, memory);
    CHECK (waitForLog (proc, "matches"));

    CHECK (proc.getCcDumpState() == OscarProcessor::CcDumpState::Active);

    // The OSCar's memory changed behind the plugin's back (sound stored on the synth):
    // the plugin shows what the OSCar plays, and sends nothing.
    proc.chooseVoice (7);
    pump (50);
    out.take();
    const auto before = proc.getDocument().getVoice (7);
    auto different = *proc.getDocument().getOscarVoice (7);
    setParam (different, *findParam ("filterFreq"), (getParam (different, *findParam ("filterFreq")) + 20) % 254);
    setParam (different, *findParam ("lfoSync"), 1);
    injectCcDump (proc, different);
    CHECK (waitForLog (proc, "updated from the OSCar"));
    CHECK (proc.getLastMessage().contains ("Filter Frequency"));
    CHECK (proc.getLastMessage().contains ("LFO Sync"));
    const auto& now = proc.getDocument().getVoice (7);
    CHECK (getParam (now, *findParam ("lfoSync")) == 1);
    CHECK (std::abs (getParam (now, *findParam ("filterFreq")) - getParam (different, *findParam ("filterFreq"))) <= 1);
    CHECK (! proc.getDocument().hasUnsentEdits (7));
    pump (600);
    CHECK (ccsOf (out.take()).empty());
    proc.getDocument().undo();                       // undo brings the plugin's previous copy back
    CHECK (proc.getDocument().getVoice (7) == before);
    pump (300);
    out.take();

    // a handful of stray CCs is not a dump
    pump (500);
    const int logSize = proc.getLog().size();
    proc.getMidi().injectForTest (juce::MidiMessage::controllerEvent (1, 30, 12));
    pump (500);
    CHECK (proc.getLog().size() == logSize);
    out.take();
}

// Edits not yet sent win over the OSCar's stored version.
static void testUnsentEditsWin (OscarProcessor& proc, Capture& out)
{
    pump (100);
    out.take();
    proc.setLiveCc (false);
    proc.editParam (8, *findParam ("filterFreq"), 40);
    proc.setLiveCc (true);
    pump (100);
    out.take();

    const auto memory = *proc.getDocument().getOscarVoice (8);
    proc.getMidi().injectForTest (juce::MidiMessage::programChange (1, 8));   // chosen on the OSCar
    injectCcDump (proc, memory);
    CHECK (waitForLog (proc, "plays the plugin's version"));
    pump (600);
    const auto ccs = ccsOf (out.take());
    int freq = 0;
    for (auto& c : ccs)
        if (c == std::make_pair (30, 20)) ++freq;
    CHECK (freq == 1 && ccs.size() == 1);
    CHECK (getParam (proc.getDocument().getVoice (8), *findParam ("filterFreq")) == 40);
}

// The reported bug: a fresh plugin, the OSCar's memory unknown, a Duo voice chosen on the OSCar.
static void testRecallWithUnknownMemory()
{
    OscarProcessor fresh;
    Capture out;
    fresh.getMidi().setTestOutput ([&out] (const juce::MidiMessage& m) { out.add (m); });
    pump (100);
    CHECK (! fresh.getDocument().isOscarVoiceStateKnown());

    auto duo = fresh.getDocument().getVoice (4);
    setParam (duo, *findParam ("function"), 2);
    setParam (duo, *findParam ("wave1"), 3);
    fresh.getMidi().injectForTest (juce::MidiMessage::programChange (1, 4));
    injectCcDump (fresh, duo);
    CHECK (waitForLog (fresh, "updated from the OSCar"));
    CHECK (fresh.getSelectedSlot() == 4);
    CHECK (getParam (fresh.getDocument().getVoice (4), *findParam ("function")) == 2);
    CHECK (getParam (fresh.getDocument().getVoice (4), *findParam ("wave1")) == 3);
    pump (600);
    CHECK (ccsOf (out.take()).empty());   // nothing pushed back to the OSCar

    // Programme changes without any dump: the indicator says the dump is off.
    fresh.getMidi().injectForTest (juce::MidiMessage::programChange (1, 5));
    pump (1700);
    fresh.getMidi().injectForTest (juce::MidiMessage::programChange (1, 6));
    pump (1700);
    CHECK (fresh.getCcDumpState() == OscarProcessor::CcDumpState::Missing);
    fresh.getMidi().setTestOutput (nullptr);
}

static void processMidi (OscarProcessor& proc, std::initializer_list<juce::MidiMessage> messages)
{
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer buffer;
    int t = 0;
    for (const auto& m : messages)
        buffer.addEvent (m, t++);
    proc.processBlock (audio, buffer);
}

static std::vector<juce::MidiMessage> notesOf (const std::vector<juce::MidiMessage>& ms)
{
    std::vector<juce::MidiMessage> r;
    for (const auto& m : ms)
        if (m.isNoteOnOrOff() || m.isPitchWheel())
            r.push_back (m);
    return r;
}

static void testMidiThru (OscarProcessor& proc, Capture& out)
{
    pump (100);
    out.take();
    proc.setChannel (3);

    // off by default: nothing goes out
    CHECK (! proc.getMidiThru());
    processMidi (proc, { juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });
    pump (100);
    CHECK (notesOf (out.take()).empty());

    proc.setMidiThru (true);

    // notes and bend are forwarded on the OSCar's channel; CCs are not
    processMidi (proc, { juce::MidiMessage::noteOn (5, 62, (juce::uint8) 90),
                         juce::MidiMessage::controllerEvent (5, 30, 12),
                         juce::MidiMessage::pitchWheel (5, 10000) });
    auto sent = collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return notesOf (ms).size() >= 2; });
    auto notes = notesOf (sent);
    CHECK (ccsOf (sent).empty());
    CHECK (notes.size() == 2 && notes[0].isNoteOn() && notes[0].getChannel() == 3
           && notes[0].getNoteNumber() == 62 && notes[0].getVelocity() == 90);
    CHECK (notes.size() == 2 && notes[1].isPitchWheel() && notes[1].getPitchWheelValue() == 10000);
    processMidi (proc, { juce::MidiMessage::noteOff (5, 62), juce::MidiMessage::pitchWheel (5, 8192) });
    notes = notesOf (collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return notesOf (ms).size() >= 2; }));
    CHECK (notes.size() == 2 && notes[0].isNoteOff() && notes[0].getNoteNumber() == 62);

    // echo of the OSCar's own keyboard coming back through the DAW: not sent again
    proc.getMidi().injectForTest (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100));
    processMidi (proc, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
    pump (60);
    processMidi (proc, { juce::MidiMessage::noteOff (1, 48) });
    pump (100);
    CHECK (notesOf (out.take()).empty());

    // during a bank transfer: new notes are dropped, the release of a held note waits for the end
    processMidi (proc, { juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) });
    notes = notesOf (collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return ! ms.empty(); }));
    CHECK (notes.size() == 1 && notes[0].isNoteOn());
    CHECK (proc.sendVoices (false).isEmpty());
    pump (50);
    processMidi (proc, { juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100) });
    pump (50);
    processMidi (proc, { juce::MidiMessage::noteOff (1, 64), juce::MidiMessage::noteOff (1, 67) });
    sent = collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return ! notesOf (ms).empty(); }, 8000);
    notes = notesOf (sent);
    CHECK (notes.size() == 1 && notes[0].isNoteOff() && notes[0].getNoteNumber() == 64);
    for (auto& m : sent)
        if (m.isNoteOn()) CHECK (m.getNoteNumber() != 67);

    // turning thru off releases what is still held
    processMidi (proc, { juce::MidiMessage::noteOn (1, 70, (juce::uint8) 100) });
    collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return ! notesOf (ms).empty(); });
    proc.setMidiThru (false);
    notes = notesOf (collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return ! notesOf (ms).empty(); }));
    CHECK (notes.size() == 1 && notes[0].isNoteOff() && notes[0].getNoteNumber() == 70);

    proc.setChannel (1);
    pump (100);
    out.take();
}

// MIDI clock from the DAW's transport: Start, 24 ticks per quarter note in time, Stop.
struct FakeHead : juce::AudioPlayHead
{
    bool playing = false;
    double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        return p;
    }
};

static void testMidiClock (OscarProcessor& proc, Capture& out)
{
    pump (100);
    out.take();

    struct Stamped { int status; double ms; };
    juce::CriticalSection lock;
    std::vector<Stamped> got;
    proc.getMidi().setTestOutput ([&] (const juce::MidiMessage& m)
    {
        const juce::ScopedLock sl (lock);
        got.push_back ({ m.getRawData()[0], juce::Time::getMillisecondCounterHiRes() });
    });
    auto take = [&]
    {
        const juce::ScopedLock sl (lock);
        auto r = got;
        got.clear();
        return r;
    };

    FakeHead head;
    proc.setPlayHead (&head);
    proc.prepareToPlay (44100.0, 512);
    juce::AudioBuffer<float> audio (2, 512);
    // Blocks are processed at the pace of real time, as a host does (a slow machine catches up).
    auto play = [&] (int blocks)
    {
        const double blockMs = 512.0 * 1000.0 / 44100.0;
        const double start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < blocks; ++i)
        {
            while (juce::Time::getMillisecondCounterHiRes() < start + i * blockMs)
                pump (1);
            juce::MidiBuffer none;
            proc.processBlock (audio, none);
            if (head.playing)
                head.ppq += 512.0 / 44100.0 * head.bpm / 60.0;
        }
        pump (1);
    };

    // off by default
    CHECK (! proc.getClockOut());
    head.playing = true;
    play (20);
    pump (60);
    CHECK (take().empty());

    // from the top at 120 BPM: Start, then 48 ticks in 86 blocks of 512 samples (just under a second)
    proc.setClockOut (true);
    head.ppq = 0;
    play (86);
    head.playing = false;
    play (2);
    pump (100);
    auto sent = take();
    int ticks = 0, starts = 0, stops = 0, others = 0;
    std::vector<double> tickTimes;
    for (const auto& e : sent)
    {
        if (e.status == 0xF8) { ++ticks; tickTimes.push_back (e.ms); }
        else if (e.status == 0xFA) ++starts;
        else if (e.status == 0xFC) ++stops;
        else ++others;
    }
    CHECK (ticks == 48 && starts == 1 && stops == 1 && others == 0);
    CHECK (! sent.empty() && sent.front().status == 0xFA && sent.back().status == 0xFC);
    if (tickTimes.size() > 2)
    {
        const double average = (tickTimes.back() - tickTimes.front()) / (double) (tickTimes.size() - 1);
        CHECK (average > 18.0 && average < 24.0);   // 20.8 ms at 120 BPM
    }

    // mid-song: Continue instead of Start
    head.playing = true;
    head.ppq = 8.0;
    play (10);
    head.playing = false;
    play (2);
    pump (100);
    sent = take();
    CHECK (! sent.empty() && sent.front().status == 0xFB && sent.back().status == 0xFC);

    // turned off while playing: Stop is sent, then nothing
    head.playing = true;
    play (5);
    proc.setClockOut (false);
    play (5);
    pump (100);
    sent = take();
    CHECK (! sent.empty() && sent.back().status == 0xFC);
    play (5);
    pump (60);
    CHECK (take().empty());

    // offline bounce: nothing goes to the hardware
    proc.setClockOut (true);
    proc.setNonRealtime (true);
    play (10);
    pump (60);
    CHECK (take().empty());
    proc.setNonRealtime (false);
    proc.setClockOut (false);
    head.playing = false;
    play (2);
    pump (60);
    take();

    // saved with the session
    proc.setClockOut (true);
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    OscarProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.getClockOut());
    proc.setClockOut (false);
    proc.setPlayHead (nullptr);

    proc.getMidi().setTestOutput ([&out] (const juce::MidiMessage& m) { out.add (m); });
}

// French interface: the translation file loads, placeholders and line breaks survive.
static void testFrench()
{
    lang::apply ("fr");
    CHECK (lang::current() == "fr");
    CHECK (TRANS ("Internal") == "Interne");
    CHECK (lang::upper (juce::String::fromUTF8 ("r\xc3\xa9glages \xc3\xa0 c\xc5\x93ur, 1/16t")) == juce::String::fromUTF8 ("R\xc3\x89GLAGES \xc3\x80 C\xc5\x92UR, 1/16T"));
    CHECK (lang::tr ("Send the DAW's MIDI clock to the OSCar") == juce::String::fromUTF8 ("Envoyer l'horloge MIDI du DAW \xc3\xa0 l'OSCar"));
    // every line of the file: key and text read back as written, line breaks and quotes included
    auto unquote = [] (juce::String q) { return q.replace ("\\\"", "\"").replace ("\\n", "\n"); };
    int lines = 0, multiLine = 0, filled = 0;
    for (const auto& line : juce::StringArray::fromLines (juce::String::fromUTF8 (BinaryData::fr_txt, BinaryData::fr_txtSize)))
    {
        if (! line.startsWith ("\""))
            continue;
        const auto key = unquote (line.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\" = \"", false, false));
        const auto text = unquote (line.fromFirstOccurrenceOf ("\" = \"", false, false).dropLastCharacters (1));
        ++lines;
        CHECK (TRANS (key) == text);
        if (key.contains ("\n"))
        {
            ++multiLine;
            CHECK (TRANS (key).contains ("\n") && ! TRANS (key).contains ("\\n"));
        }
        if (key.contains ("%1"))
        {
            ++filled;
            CHECK (lang::tr (key, { "<<X>>" }).contains ("<<X>>"));
        }
    }
    CHECK (lines > 300 && multiLine > 0 && filled > 0);
    lang::apply ("en");
    CHECK (TRANS ("Internal") == "Internal");
}

// The editor opens on every page, at several sizes, and sends nothing by itself.
static void testEditor (OscarProcessor& proc, Capture& out)
{
    pump (100);
    out.take();
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<OscarEditor*> (ed.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    for (auto size : { 1400, 980, 2100 })
    {
        ed->setSize (size, size * 900 / 1400);
        for (int page = 0; page < 7; ++page)
        {
            editor->showPage (page);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
            CHECK (img.isValid());
        }
    }
    proc.generate();      // through the generator, with the editor listening
    pump (200);
    proc.getDocument().undo();
    pump (200);

    // saving a voice or a .syx bank goes straight into its folder (after the name dialog)
    const auto voices = OscarProcessor::getVoicesFolder(), banks = OscarProcessor::getBanksFolder();
    auto voice = [editor] (const juce::File& f) { return editor->exportVoiceTo (f); };
    const auto v1 = editor->saveNamed (voices, ".oscvoice", "Test: voice", voice);   // ':' is not allowed in file names
    const auto v2 = editor->saveNamed (voices, ".oscvoice", "Test: voice", voice);   // never overwrites
    CHECK (v1 == voices.getChildFile ("Test voice.oscvoice") && v1.existsAsFile());
    CHECK (v2 != v1 && v2.existsAsFile() && v2.getParentDirectory() == voices);
    CHECK (editor->saveNamed (voices, ".oscvoice", "   ", voice) == juce::File());   // empty name: nothing
    CHECK (voices.findChildFiles (juce::File::findFiles, false, "*.oscvoice").size() == 2);
    CHECK (proc.getDocument().importVoice (3, v1).wasOk());
    const auto bank = editor->saveNamed (banks, ".syx", "Test bank", [editor] (const juce::File& f) { return editor->exportSyxTo (f); });
    CHECK (bank == banks.getChildFile ("Test bank.syx") && bank.existsAsFile());
    juce::String report;
    CHECK (proc.getDocument().loadFile (bank, report).wasOk());
    editor->showPage (OscarEditor::banksPageIndex);   // the library lists them
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
    CHECK (img.isValid());
    pump (100);

    // waveforms received from the OSCar are loaded even when they read like a text copy: MIDI can't do that
    {
        // the waveform block of the damaged factory archive (Tests/factory_codemechanic.syx): 27 pairs 0D 0A
        const juce::String hex ("02070b070e0909020e08070303000800060d0a000d0a03000b080b010c040804000007010b0d0a060f0b03050b05050d0a0d0a0f0f0804"
                                "01000e09040f0e070304000c0508060102010e0d0a0f01070506050c060e0b020d0a0b010d0a020e0d0a0d0a0305040d0a0d0a030b040f"
                                "0f0206030c0f080d0a080d0a0d0a05080305010b060e0507010803010c000d0a0b0d0a050702030d0a0304020d0a0001070e0c0406010102"
                                "0f010b0607060c0c05060e080e040d0a010705080d0a0307080d0a0d0a0c00000e0401010e090000000d0a080f000b0003080d0a0000070b"
                                "04070c0e0000020f0d0a050b090001060d0a");
        std::vector<std::uint8_t> body { 0x2B, 0x01 };
        for (int i = 0; i + 1 < hex.length(); i += 2)
            body.push_back ((std::uint8_t) hex.substring (i, i + 2).getHexValue32());
        CHECK (body.size() == 242);
        proc.getMidi().injectForTest (juce::MidiMessage::createSysExMessage (body.data(), (int) body.size()));
        CHECK (waitForLog (proc, "aveform"));
        CHECK (proc.getDocument().getWaves().has_value()
               && std::equal (proc.getDocument().getWaves()->raw.begin(), proc.getDocument().getWaves()->raw.end(), body.begin() + 2));
        CHECK (! proc.getLastMessage().contains ("CR/LF"));
    }

    // the Waveforms page: one quick stroke across the bands draws every band it crosses
    if (! proc.getDocument().getWaves())
    {
        DumpContents d;
        d.waves = WaveBank {};
        proc.getDocument().receiveDump (d);
    }
    editor->showPage (4);
    panels::WavesPanel* waves = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* w = dynamic_cast<panels::WavesPanel*> (child)) waves = w;
            else find (*child);
        }
    };
    find (*ed);
    CHECK (waves != nullptr && proc.getDocument().getWaves().has_value());
    if (waves != nullptr && proc.getDocument().getWaves())
    {
        const auto b = waves->barsArea();
        const float band = b.getWidth() / (float) kNumHarmonics;
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto event = [&] (juce::Point<float> p, juce::Point<float> down)
        {
            return juce::MouseEvent (src, p, {}, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                     juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     waves, waves, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, true);
        };
        const juce::Point<float> from { b.getX() + band * 2.5f, b.getBottom() - 2.0f },   // band 2 at the bottom
                                 to { b.getX() + band * 10.5f, b.getY() + 2.0f };          // band 10 at the top
        waves->mouseDown (event (from, from));
        waves->mouseDrag (event (to, from));
        const auto& w = *proc.getDocument().getWaves();
        const int t = waves->currentTable();
        CHECK (w.getHarmonic (t, 2) == 0 && w.getHarmonic (t, 10) == 255);
        bool rising = true;
        for (int h = 3; h <= 10; ++h)
            rising = rising && w.getHarmonic (t, h) > w.getHarmonic (t, h - 1);
        CHECK (rising);   // bands 3 to 9 on the line between
        // past the last band: the stroke goes on to it
        waves->mouseDrag (event ({ b.getRight() + 40.0f, b.getCentreY() }, from));
        CHECK (proc.getDocument().getWaves()->getHarmonic (t, kNumHarmonics - 1) > 100);
    }

    ed.reset();
    pump (100);
    CHECK (countPrograms (out.take(), -1) == 0);
}

static void testAutomation (OscarProcessor& proc, Capture& out)
{
    pump (100);
    out.take();

    setHost (proc, "cc_filterFreq", 1.0f);           // 254 -> CC 127
    auto ccs = ccsOf (collect (out, ccCountAtLeast (1)));
    CHECK (ccs.size() == 1 && ccs[0] == std::make_pair (30, 127));

    // a choice parameter: Filter Type "HP" (value 2)
    auto* ft = proc.getHostParameters().getParameter ("cc_filterType");
    ft->setValueNotifyingHost (ft->convertTo0to1 (2.0f));
    ccs = ccsOf (collect (out, ccCountAtLeast (1)));
    CHECK (ccs.size() == 1 && ccs[0].first == 33 && firmwareValueFromCc (CcCoding::SixSparse, ccs[0].second) == 2);

    // arpeggiator automation: 51 then 50
    auto* am = proc.getHostParameters().getParameter ("cc_arpMode");
    am->setValueNotifyingHost (am->convertTo0to1 (1.0f));
    ccs = ccsOf (collect (out, ccCountAtLeast (2)));
    CHECK (ccs.size() == 2 && ccs[0].first == 51 && ccs[1].first == 50);

    // tempo (global)
    setHost (proc, "tempo", 0.0f);
    ccs = ccsOf (collect (out, ccCountAtLeast (1)));
    CHECK (ccs.size() == 1 && ccs[0].first == kTempoCc);

    // live CC off: automation is not sent, and turning it back on doesn't replay it
    proc.setLiveCc (false);
    setHost (proc, "cc_resonance", 0.25f);
    pump (150);
    CHECK (out.take().empty());
    proc.setLiveCc (true);
    pump (150);
    CHECK (out.take().empty());
}

static void testSysexAndOrdering (OscarProcessor& proc, Capture& out)
{
    pump (100);
    out.take();
    proc.setSelectedSlot (12);
    const auto expected = buildVoiceMessage (proc.getDocument().getBank());
    CHECK (proc.sendVoices (true).isEmpty());
    CHECK (proc.getDocument().getVoiceSync() == BankDocument::Sync::InSync);

    // an edit during the transfer waits for the end of the SysEx
    proc.editParam (12, *findParam ("detune"), 40);
    pump (100);
    auto sent = out.take();
    CHECK (sent.size() == 1 && sent[0].isSysEx());
    CHECK (! sent.empty() && sent[0].getRawDataSize() == (int) expected.size()
           && std::equal (expected.begin(), expected.end(), sent[0].getRawData()));

    // a second send is refused while busy
    CHECK (proc.sendVoices (false).isNotEmpty());

    // transfer (~1.1 s), then the programme change, then the pending edit
    sent = collect (out, [] (const std::vector<juce::MidiMessage>& ms)
    {
        for (auto& c : ccsOf (ms)) if (c.first == 17) return true;
        return false;
    }, 8000);
    CHECK (countPrograms (sent, 12) == 1);
    bool detune = false, noSecondSysex = true;
    for (auto& c : ccsOf (sent))
        if (c.first == 17) detune = true;
    for (auto& m : sent)
        if (m.isSysEx()) noSecondSysex = false;   // no second SysEx slipped in
    CHECK (detune && noSecondSysex);
}

static void testState (OscarProcessor& proc, Capture&)
{
    juce::MemoryBlock state;
    proc.getStateInformation (state);

    OscarProcessor other;
    Capture out2;
    other.getMidi().setTestOutput ([&out2] (const juce::MidiMessage& m) { out2.add (m); });
    other.setStateInformation (state.getData(), (int) state.getSize());
    pump (300);
    CHECK (out2.take().empty());                     // restoring sends nothing
    CHECK (other.getDocument().getBank() == proc.getDocument().getBank());
    CHECK (other.getDocument().getVoiceSync() == BankDocument::Sync::Unknown);
    CHECK (other.getSelectedSlot() == proc.getSelectedSlot());
    CHECK (juce::approximatelyEqual (other.getHostParameters().getParameter ("cc_filterFreq")->getValue(), 1.0f));
    other.getMidi().setTestOutput (nullptr);
}

// The A/B morph: knobs glide, switches flip half way, the CC are paced, Undo brings the voice back.
static std::vector<int> ccValues (const std::vector<juce::MidiMessage>& ms, int cc)
{
    std::vector<int> r;
    for (const auto& [n, v] : ccsOf (ms))
        if (n == cc) r.push_back (v);
    return r;
}

static void testMorph (OscarProcessor& proc, Capture& out)
{
    auto& doc = proc.getDocument();
    const int target = 20, slotA = 3, slotB = 4;
    VoiceData va = doc.getVoice (slotA), vb = doc.getVoice (slotB);
    for (const auto& p : allParams())   // A at the bottom of every knob, B at the top; different waves
        if (p.kind == ParamKind::Knob)
        {
            setParam (va, p, 0);
            setParam (vb, p, kKnobMaxEditable);
        }
    setParam (va, *findParam ("wave1"), 0);
    setParam (vb, *findParam ("wave1"), 2);
    doc.setVoice (slotA, va);
    doc.setVoice (slotB, vb);
    proc.setLiveCc (true);
    proc.chooseVoice (target);
    pump (700);
    out.take();
    const auto original = doc.getVoice (target);
    const auto& filterFreq = *findParam ("filterFreq");
    auto lastFreqCc = [&filterFreq] (const std::vector<juce::MidiMessage>& ms)
    {
        int last = -1;
        for (const auto& [cc, value] : ccsOf (ms))
            if (cc == filterFreq.cc) last = value;
        return last;
    };

    proc.setMorphSources (slotA, slotB);
    proc.setMorphSmoothing (0.0f);   // instant: the pacing alone spreads the CC
    auto* morph = proc.getHostParameters().getParameter ("morph");

    // off (the default): the fader and its automation do nothing
    CHECK (! proc.getMorphOn());
    morph->setValueNotifyingHost (0.25f);
    pump (400);
    CHECK (ccsOf (out.take()).empty() && doc.getVoice (target) == original);

    // on: the OSCar plays the blend, paced; no voice changes
    proc.setMorphOn (true);
    std::vector<size_t> batches;
    std::vector<juce::MidiMessage> all;
    for (int i = 0; i < 100; ++i)
    {
        pump (10);
        auto got = out.take();
        if (! got.empty()) batches.push_back (ccsOf (got).size());
        all.insert (all.end(), got.begin(), got.end());
    }
    CHECK (! batches.empty() && batches.front() <= 24);                               // paced, never all at once
    CHECK (lastFreqCc (all) == ccValueFor (filterFreq.coding, 64));                  // a quarter of the way
    CHECK (doc.getVoice (target) == original);
    CHECK (getParam (proc.getMorphBlend(), *findParam ("wave1")) == 0);               // still A's wave

    morph->setValueNotifyingHost (1.0f);
    pump (1000);
    CHECK (lastFreqCc (out.take()) == ccValueFor (filterFreq.coding, kKnobMaxEditable));
    CHECK (doc.getVoice (target) == original);
    CHECK (OscarProcessor::blend (va, vb, 0.5f) != OscarProcessor::blend (va, vb, 0.49f));

    // edits of the selected voice are stored, not sent: the blend plays
    proc.editParam (target, *findParam ("resonance"), 77);
    pump (200);
    CHECK (ccValues (out.take(), findParam ("resonance")->cc).empty());
    proc.editParam (target, *findParam ("resonance"), getParam (original, *findParam ("resonance")));
    doc.endUndoGroup();

    // keep: the blend goes into the selected voice, one undo step
    const auto beforeKeep = doc.getVoice (target);
    proc.morphKeep();
    pump (100);
    CHECK (getParam (doc.getVoice (target), filterFreq) == kKnobMaxEditable);
    CHECK (getParam (doc.getVoice (target), *findParam ("wave1")) == 2);
    doc.undo();
    pump (200);
    CHECK (doc.getVoice (target) == beforeKeep);

    // a voice loaded at one end: the sound glides there (A and C both at the bottom)
    const int slotC = 5;
    VoiceData vc = va;
    setParam (vc, *findParam ("wave1"), 3);
    doc.setVoice (slotC, vc);
    morph->setValueNotifyingHost (0.5f);
    pump (400);
    proc.loadMorphSource (true, slotC);
    pump (600);
    CHECK (proc.morphSettings().slotB == slotC);
    CHECK (getParam (proc.getMorphBlend(), filterFreq) == 0);
    CHECK (getParam (proc.getMorphBlend(), *findParam ("wave1")) == 3);   // half way: C's wave
    out.take();

    // off: the OSCar gets the selected voice back
    proc.setMorphOn (false);
    pump (600);
    CHECK (lastFreqCc (out.take()) == ccValueFor (filterFreq.coding, getParam (doc.getVoice (target), filterFreq)));
    morph->setValueNotifyingHost (0.0f);
    pump (300);
    CHECK (ccsOf (out.take()).empty());

    // choosing another voice turns MORPH off
    proc.setMorphOn (true);
    pump (300);
    proc.chooseVoice (target == 21 ? 22 : 21);
    pump (200);
    CHECK (! proc.getMorphOn());
    proc.chooseVoice (target);
    pump (700);
    doc.setVoice (target, original);
    pump (300);
    out.take();
}

// RANDOM: values sent at each step, free or looped, put back at 0; SCALE: Transpose steps through
// it, its randoms land in it, MIDI Thru's notes move to it. (No DAW playing: the steps run on.)
static void testRandom (OscarProcessor& proc, Capture& out)
{
    auto& doc = proc.getDocument();
    const int target = 7;
    proc.setLiveCc (true);
    proc.chooseVoice (target);
    pump (700);
    out.take();
    const auto& freq = *findParam ("filterFreq");
    doc.setParam (target, freq, 127);
    const int baseCc = ccValueFor (freq.coding, 127);
    auto hasBase = [&freq, baseCc] (const std::vector<juce::MidiMessage>& ms)
    {
        const auto v = ccValues (ms, freq.cc);
        return std::find (v.begin(), v.end(), baseCc) != v.end();
    };
    pump (100);
    out.take();

    // free: a new value at each step (1/32 at 120 BPM: 16 a second)
    proc.setRandomBpm (120);
    proc.setRandomRate (0);
    proc.setRandomAmount (freq, 100);
    pump (1000);
    auto values = ccValues (out.take(), freq.cc);
    CHECK (values.size() >= 10 && values.size() <= 22);
    CHECK (std::set<int> (values.begin(), values.end()).size() >= 5);
    CHECK (proc.isRandomPlaying() && proc.getRandomSteps() > 0);
    CHECK (getParam (doc.getVoice (target), freq) == 127);   // the voice stays

    // looped: the same four values over and over (turned down: the shape upside down)
    proc.setRandomLoop (4);
    proc.setRandomAmount (freq, -50);
    pump (60);
    out.take();
    pump (1200);
    values = ccValues (out.take(), freq.cc);
    CHECK (values.size() >= 12);
    bool loops = values.size() >= 8;
    for (size_t i = 4; i < values.size(); ++i)
        loops = loops && values[i] == values[i - 4];
    CHECK (loops);
    CHECK (std::set<int> (values.begin(), values.end()).size() >= 2);
    const auto firstLoop = std::vector<int> (values.begin(), values.begin() + juce::jmin<std::ptrdiff_t> (4, (std::ptrdiff_t) values.size()));

    // back to 0: its own value again, then nothing
    proc.setRandomAmount (freq, 0);
    values = ccValues (collect (out, hasBase), freq.cc);
    CHECK (values.size() <= 2 && ! values.empty() && values.back() == baseCc);
    pump (300);
    CHECK (ccsOf (out.take()).empty() && ! proc.isRandomPlaying());

    // turned left again: another loop (the way back to 0 drew it)
    proc.setRandomAmount (freq, -50);
    pump (60);
    out.take();
    pump (800);
    values = ccValues (out.take(), freq.cc);
    bool sameLoop = values.size() >= 8;
    for (size_t i = 0; i + 4 < values.size() && sameLoop; ++i)
        sameLoop = std::find (firstLoop.begin(), firstLoop.end(), values[i]) != firstLoop.end();
    CHECK (! sameLoop);

    // saved with the session, loops included
    proc.setScale (3);
    proc.setScaleRoot (2);
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    {
        OscarProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        pump (50);
        CHECK (other.getRandomAmount (freq) == -50 && other.randomSettings().loop == 4 && other.randomSettings().rate == 0);
        CHECK (other.randomSettings().scale == 3 && other.randomSettings().root == 2 && other.hasRandoms());
    }

    // Live CC off: the OSCar gets the voice back, and nothing more
    proc.setLiveCc (false);
    values = ccValues (collect (out, hasBase), freq.cc);
    CHECK (! values.empty() && values.back() == baseCc);
    pump (300);
    CHECK (ccsOf (out.take()).empty());
    proc.setLiveCc (true);
    proc.clearRandoms();
    CHECK (! proc.hasRandoms());
    pump (300);
    out.take();

    // a wave: SINE, a cycle a beat (half a second at 120 BPM): the knob sweeps its whole range, paced
    proc.setRandomShape (2);
    proc.setRandomRate (6);
    proc.setRandomAmount (freq, 50);
    pump (1500);
    values = ccValues (out.take(), freq.cc);
    CHECK (values.size() >= 40 && values.size() <= 650);
    CHECK (! values.empty() && *std::min_element (values.begin(), values.end()) < 15 && *std::max_element (values.begin(), values.end()) > 112);
    // free rate (not synced): a cycle every 300 ms, whatever the tempo
    proc.setRandomSync (false);
    proc.setRandomFreeMs (300.0);
    pump (100);
    out.take();
    const int stepsBefore = proc.getRandomSteps();
    pump (1500);
    CHECK (proc.getRandomSteps() - stepsBefore >= 4 && proc.getRandomSteps() - stepsBefore <= 6);
    proc.setRandomSync (true);
    // unipolar, turned up: the knob only goes above its own value (127 -> CC 63 and up)
    proc.setRandomBipolar (false);
    pump (100);
    out.take();
    pump (800);
    values = ccValues (out.take(), freq.cc);
    CHECK (! values.empty() && *std::min_element (values.begin(), values.end()) >= 62);
    proc.setRandomBipolar (true);
    // SMOOTH: from one random value to the next, without jumps
    proc.setRandomShape (1);
    proc.setRandomRate (4);
    pump (100);
    out.take();
    pump (1000);
    values = ccValues (out.take(), freq.cc);
    CHECK (values.size() >= 20);
    int jump = 0;
    for (size_t i = 1; i < values.size(); ++i)
        jump = juce::jmax (jump, std::abs (values[i] - values[i - 1]));
    CHECK (jump <= 12);
    // switched off (right-click), then all of them (ACTIVE off): the voice's value, the amount kept
    proc.setRandomShape (0);
    proc.setRandomRate (0);
    pump (200);
    CHECK (proc.getRandomShown (freq) >= 0);
    proc.setRandomMuted (freq, true);
    values = ccValues (collect (out, hasBase), freq.cc);
    CHECK (! values.empty() && values.back() == baseCc);
    pump (300);
    CHECK (ccValues (out.take(), freq.cc).empty() && proc.getRandomShown (freq) < 0 && proc.getRandomAmount (freq) == 50);
    proc.setRandomMuted (freq, false);
    pump (400);
    CHECK (ccValues (out.take(), freq.cc).size() >= 3);
    proc.setRandomBypass (true);
    values = ccValues (collect (out, hasBase), freq.cc);
    CHECK (! values.empty() && values.back() == baseCc);
    pump (300);
    CHECK (ccValues (out.take(), freq.cc).empty() && proc.hasRandoms());
    proc.setRandomMuted (freq, true);
    {
        juce::MemoryBlock saved;
        proc.getStateInformation (saved);
        OscarProcessor other;
        other.setStateInformation (saved.getData(), (int) saved.getSize());
        pump (50);
        CHECK (other.getRandomBypass() && other.isRandomMuted (freq) && other.getRandomAmount (freq) == 50);
    }
    proc.setRandomBypass (false);
    pump (300);
    CHECK (ccValues (out.take(), freq.cc).empty());   // still switched off on its own
    proc.setRandomAmount (freq, 0);                    // removed: no longer switched off
    CHECK (! proc.isRandomMuted (freq));
    pump (300);
    out.take();

    // SCALE (C major): Transpose steps from note to note of it, both ways
    const auto& tune = *findParam ("tuneSemi");
    proc.setScale (1);
    proc.setScaleRoot (0);
    proc.editParam (target, tune, valueOfSemis (0));
    proc.editParam (target, tune, valueOfSemis (1));
    CHECK (semisOfValue (getParam (doc.getVoice (target), tune)) == 2);
    proc.editParam (target, tune, valueOfSemis (1));
    CHECK (semisOfValue (getParam (doc.getVoice (target), tune)) == 0);
    pump (200);
    out.take();
    // its random lands on the scale too
    proc.setRandomAmount (tune, 100);
    pump (1000);
    proc.setRandomAmount (tune, 0);
    pump (200);
    values = ccValues (out.take(), tune.cc);
    CHECK (values.size() >= 8);
    bool inMajor = true;
    for (auto v : values)
        inMajor = inMajor && inScale (semisOfValue (firmwareValueFromCc (tune.coding, v)), scales()[1].mask, 0);
    CHECK (inMajor);

    // MIDI Thru: notes move to the scale; two keys on one note release it once
    proc.setMidiThru (true);
    processMidi (proc, { juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100) });
    auto notes = notesOf (collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return ! notesOf (ms).empty(); }));
    CHECK (notes.size() == 1 && notes[0].isNoteOn() && notes[0].getNoteNumber() == 60);
    processMidi (proc, { juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });
    pump (60);
    processMidi (proc, { juce::MidiMessage::noteOff (1, 61) });
    pump (60);
    notes = notesOf (out.take());
    CHECK (notes.size() == 1 && notes[0].isNoteOn() && notes[0].getNoteNumber() == 60);
    processMidi (proc, { juce::MidiMessage::noteOff (1, 60) });
    notes = notesOf (collect (out, [] (const std::vector<juce::MidiMessage>& ms) { return ! notesOf (ms).empty(); }));
    CHECK (notes.size() == 1 && notes[0].isNoteOff() && notes[0].getNoteNumber() == 60);
    proc.setMidiThru (false);
    proc.setScale (0);
    pump (200);
    out.take();
}

int main()
{
    // Never run against the user's real settings, MIDI ports or backup folder.
    const auto testDir = juce::SystemStats::getEnvironmentVariable ("OSCAR_EDITOR_TEST_DIR", {});
    if (testDir.isEmpty())
    {
        std::printf ("OSCAR_EDITOR_TEST_DIR is not set: run this test through ctest.\n");
        return 2;
    }
    juce::File (testDir).deleteRecursively();
    juce::File (testDir).createDirectory();

    juce::ScopedJuceInitialiser_GUI init;

    // OSCAR_BENCH=1: only times full repaints of each page at 2x (what a click costs at worst).
    if (juce::SystemStats::getEnvironmentVariable ("OSCAR_BENCH", {}).isNotEmpty())
    {
        OscarProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (1400, 900);
        juce::Image img (juce::Image::ARGB, 2800, 1800, true);
        for (int page = 0; page < 7; ++page)
        {
            dynamic_cast<OscarEditor*> (ed.get())->showPage (page);
            pump (50);
            {
                juce::Graphics g (img);   // the first paint fills the caches (wood, wear, logo)
                g.addTransform (juce::AffineTransform::scale (2.0f));
                ed->paintEntireComponent (g, false);
            }
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 10; ++i)
            {
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                ed->paintEntireComponent (g, false);
            }
            std::printf ("page %d: %.1f ms per full repaint\n", page, (juce::Time::getMillisecondCounterHiRes() - t0) / 10.0);
            if (page == 0)   // where the time goes: each component on its own (children included)
            {
                std::map<juce::String, double> byType;
                std::function<void (juce::Component&, int)> walk = [&] (juce::Component& c, int depth)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (! child->isVisible())
                            continue;
                        juce::Image part (juce::Image::ARGB, juce::jmax (1, child->getWidth() * 2), juce::jmax (1, child->getHeight() * 2), true);
                        const auto t = juce::Time::getMillisecondCounterHiRes();
                        for (int i = 0; i < 5; ++i)
                        {
                            juce::Graphics pg (part);
                            pg.addTransform (juce::AffineTransform::scale (2.0f));
                            child->paintEntireComponent (pg, false);
                        }
                        const double ms = (juce::Time::getMillisecondCounterHiRes() - t) / 5.0;
                        if (depth < 2)
                            std::printf ("  %*s%s %dx%d: %.1f ms\n", depth * 2, "", typeid (*child).name(), child->getWidth(), child->getHeight(), ms);
                        if (depth == 2)
                            byType[typeid (*child).name()] += ms;
                        if (depth < 2)
                            walk (*child, depth + 1);
                    }
                };
                walk (*ed, 0);
                for (const auto& [t, ms] : byType)
                    std::printf ("    all %s: %.1f ms\n", t.toRawUTF8(), ms);
            }
        }
        ed.reset();
        pump (100);
        return 0;
    }

    // OSCAR_SHOTS=<folder> [OSCAR_SHOTS_SCALE=2] [OSCAR_SHOTS_LANG=fr] [OSCAR_SHOTS_FINISH=0..3]: only saves a PNG of each page.
    if (const auto shots = juce::SystemStats::getEnvironmentVariable ("OSCAR_SHOTS", {}); shots.isNotEmpty())
    {
        atelier::applyFinish (juce::SystemStats::getEnvironmentVariable ("OSCAR_SHOTS_FINISH", "0").getIntValue());
        OscarProcessor proc;
        proc.setLanguage (juce::SystemStats::getEnvironmentVariable ("OSCAR_SHOTS_LANG", "en"));
        pump (100);
        oscar::DumpContents demo;   // some waveforms, so the Waveforms page has something to show
        oscar::WaveBank w;
        for (int t = 0; t < kNumWaveTables; ++t)
            for (int h = 0; h < kNumHarmonics; ++h)
                w.setHarmonic (t, h, juce::jlimit (0, 255, juce::roundToInt (255.0 / std::pow (h + 1.0, 0.8 + 0.15 * t)
                                                                               * (h % 2 == 0 || t == 0 ? 1.0 : 0.35 + 0.5 * std::sin (h * 0.9) * std::sin (h * 0.9)))));
        demo.waves = w;
        oscar::VoiceBank voices;   // and as if the OSCar had just sent its voices, with a MIDI output open
        for (int i = 0; i < kNumVoices; ++i)
            voices.voices[(size_t) i] = proc.getDocument().getVoice (i);
        demo.voices = voices;
        proc.getDocument().receiveDump (demo);
        proc.getMidi().setTestOutput ([] (const juce::MidiMessage&) {});
        proc.setSelectedSlot (juce::SystemStats::getEnvironmentVariable ("OSCAR_SHOTS_SLOT", "12").getIntValue());
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (1400, 900);
        const float scale = juce::SystemStats::getEnvironmentVariable ("OSCAR_SHOTS_SCALE", "1").getFloatValue();
        juce::File (shots).createDirectory();
        auto shoot = [&] (const juce::String& name)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            juce::FileOutputStream os (juce::File (shots).getChildFile (name + ".png"));
            os.setPosition (0);
            os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
        };
        for (int page = 0; page < 7; ++page)
        {
            dynamic_cast<OscarEditor*> (ed.get())->showPage (page);
            pump (50);
            shoot ("page" + juce::String (page));
        }
        // OSCAR_SHOTS_RANDOM=1: the Voice page again with a few randoms and a scale, then in the RANDOM layer
        if (juce::SystemStats::getEnvironmentVariable ("OSCAR_SHOTS_RANDOM", {}).isNotEmpty())
        {
            for (const auto& [id, amount] : { std::pair<const char*, int> { "filterFreq", 60 }, { "resonance", -40 }, { "tuneSemi", 50 },
                                              { "filtDecay", -70 }, { "lfoRate", 30 }, { "osc2Semi", -35 } })
                proc.setRandomAmount (*findParam (id), amount);
            proc.setRandomMuted (*findParam ("lfoRate"), true);   // switched off: a dark LED
            proc.setScale (2);
            proc.setScaleRoot (9);
            proc.setLiveCc (true);                                // playing: lit along their scale
            auto* oe = dynamic_cast<OscarEditor*> (ed.get());
            oe->showPage (0);
            proc.addLog ("RANDOM");   // the editor refreshes
            pump (300);
            shoot ("random-marks");
            oe->toggleRandomEdit();
            pump (100);
            shoot ("random-layer");
            oe->toggleScaleStrip();
            pump (100);
            shoot ("scale-strip");
            oe->openSave (true);
            pump (100);
            shoot ("save-oscar");
            proc.storeStart (5);
            pump (900);
            shoot ("save-waiting");
            proc.storeCancel();
            oe->openSave (false);
            pump (100);
            shoot ("save-computer");
        }
        ed.reset();
        proc.getMidi().setTestOutput (nullptr);
        pump (100);
        return 0;
    }

    {
        OscarProcessor proc;
        Capture out;
        proc.getMidi().setTestOutput ([&out] (const juce::MidiMessage& m) { out.add (m); });

        testLiveEditing (proc, out);
        testKnownState (proc, out);
        testCcDump (proc, out);
        testUnsentEditsWin (proc, out);
        testEditor (proc, out);
        testAutomation (proc, out);
        testSysexAndOrdering (proc, out);
        testState (proc, out);
        testMidiThru (proc, out);
        testMidiClock (proc, out);
        testMorph (proc, out);
        testRandom (proc, out);

        // recall: a project opens, the OSCar gets the saved voice whole (no programme change)
        {
            auto& doc = proc.getDocument();
            const int slot = proc.getSelectedSlot();
            const auto& freq = *findParam ("filterFreq");
            doc.setParam (slot, freq, 120);
            pump (300);
            juce::MemoryBlock saved;
            proc.getStateInformation (saved);
            auto reopen = [&]
            {
                doc.setParam (slot, freq, 30);             // changed after saving, not saved
                pump (300);
                out.take();
                proc.setStateInformation (saved.getData(), (int) saved.getSize());
                CHECK (getParam (doc.getVoice (slot), freq) == 120);
            };
            auto checkRecalled = [&]
            {
                auto sent = collect (out, ccCountAtLeast (40));
                CHECK (countPrograms (sent, -1) == 0);
                CHECK (ccsOf (sent).size() >= 40);
                CHECK (ccValues (sent, freq.cc).size() == 1 && ! ccValues (sent, freq.cc).empty() && ccValues (sent, freq.cc).back() == 60);
                CHECK (waitForLog (proc, "Sound recalled"));
            };

            // ask first (the default): nothing goes until the answer
            CHECK (proc.getRecallMode() == OscarProcessor::RecallMode::Ask);
            reopen();
            pump (1000);
            CHECK (proc.isRecallWaiting() && ccsOf (out.take()).empty());
            proc.answerRecall (true);
            checkRecalled();
            reopen();
            pump (200);
            proc.answerRecall (false);                     // keep the OSCar's sound
            pump (1000);
            CHECK (! proc.isRecallWaiting() && ccsOf (out.take()).empty());

            // always: at once
            proc.setRecallMode (OscarProcessor::RecallMode::Always);
            reopen();
            checkRecalled();

            // never
            proc.setRecallMode (OscarProcessor::RecallMode::Never);
            reopen();
            pump (1000);
            CHECK (! proc.isRecallWaiting() && ccsOf (out.take()).empty());
            proc.recallNow();                              // the Setup page's button
            checkRecalled();
            proc.setRecallMode (OscarProcessor::RecallMode::Ask);
        }

        // the OSCar on another MIDI channel: the plugin says so, until it hears it on its own
        proc.setChannel (1);
        proc.getMidi().injectForTest (juce::MidiMessage::controllerEvent (5, 7, 0));
        CHECK (waitForLog (proc, "channel 5"));
        CHECK (proc.channelMismatch() && proc.getOscarChannel() == 5);
        proc.setChannel (proc.getOscarChannel());   // what "Use channel 5" does
        CHECK (! proc.channelMismatch() && proc.getChannel() == 5);
        proc.setChannel (1);
        proc.getMidi().injectForTest (juce::MidiMessage::controllerEvent (1, 7, 0));
        for (int i = 0; i < 200 && proc.channelMismatch(); ++i)   // the next check of the 40 ms timer (slow machines: later)
            pump (10);
        CHECK (! proc.channelMismatch());

        // SAVE, in the OSCar: the voice goes whole, SUPERMOD holds, the user presses the keys, Done
        {
            auto& doc = proc.getDocument();
            const auto& freq = *findParam ("filterFreq");
            const auto& res = *findParam ("resonance");
            const int source = 7, target = 20;
            proc.chooseVoice (source);
            pump (300);
            doc.setParam (source, freq, 171);                 // odd: the OSCar gets 170 over CC
            doc.setParam (source, res, 0);                    // 0: the OSCar gets 1
            doc.rename (source, "Store me");
            proc.setLiveCc (true);
            proc.setRandomShape (2);                          // SINE: it plays all the time
            proc.setRandomAmount (freq, 60);
            pump (400);
            CHECK (proc.getRandomShown (freq) >= 0);
            out.take();
            proc.storeStart (target);
            CHECK (proc.getStoreTarget() == target);
            auto sent = collect (out, ccCountAtLeast (40));
            CHECK (ccsOf (sent).size() >= 40 && ! ccValues (sent, freq.cc).empty() && ccValues (sent, freq.cc).back() == 171 >> 1);
            CHECK (waitForLog (proc, "STORE, then key " + juce::String (slotLabel (target))));
            pump (300);
            CHECK (proc.getRandomShown (freq) < 0 && ccValues (out.take(), freq.cc).empty());   // held while the keys are pressed
            proc.storeDone();
            CHECK (proc.getStoreTarget() < 0);
            CHECK (getParam (doc.getVoice (target), freq) == 170 && getParam (doc.getVoice (target), res) == 1);
            CHECK (doc.getVoice (source) != doc.getVoice (target) || source == target);   // the source keeps its own values
            CHECK (doc.getName (target) == "Store me" && ! doc.hasUnsentEdits (target));
            CHECK (waitForLog (proc, "is in the OSCar's memory " + juce::String (slotLabel (target))));
            pump (300);
            CHECK (proc.getRandomShown (freq) >= 0);           // SUPERMOD plays again
            doc.undo();                                         // the name, then the voice
            doc.undo();
            CHECK (doc.getName (target) != "Store me" && doc.hasUnsentEdits (target));
            proc.storeStart (target);                           // cancelled (the strip closed)
            pump (100);
            proc.storeCancel();
            CHECK (proc.getStoreTarget() < 0);
            proc.clearRandoms();
            proc.setRandomShape (0);
            pump (300);
        }

        proc.getMidi().setTestOutput (nullptr);
    }

    testRecallWithUnknownMemory();
    testFrench();

    // backups went to the test folder: one received and one sent bank
    const auto backups = juce::File (testDir).getChildFile ("Backups").findChildFiles (juce::File::findFiles, false, "*.syx");
    CHECK (backups.size() >= 2);

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
