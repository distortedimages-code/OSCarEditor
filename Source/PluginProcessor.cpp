#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Lang.h"

#include <cstring>

using lang::tr;

using namespace oscar;

namespace
{
    const juce::Identifier idState ("OSCarEditorState"), idOutId ("outId"), idOutName ("outName"),
                           idInId ("inId"), idInName ("inName"), idChannel ("channel"),
                           idSlot ("slot"), idFollow ("follow"), idSelectAfter ("selectAfter"),
                           idLiveCc ("liveCc"), idThru ("midiThru"), idClock ("clockOut"), idHost ("HostParams"),
                           idRandom ("Random"), idRandomParam ("Param"), idId ("id"), idAmount ("amount"), idSeed ("seed"),
                           idRate ("rate"), idShape ("shape"), idLoop ("loop"), idBpm ("bpm"), idScale ("scale"), idRoot ("root");

    // A note the OSCar's own keyboard just played that comes back through the DAW
    // (track input monitoring) is an echo: it must not be sent to the OSCar again.
    constexpr double kEchoWindowMs = 150.0;

    juce::StringArray differingParams (const VoiceData& a, const VoiceData& b)
    {
        juce::StringArray names;
        for (const auto& p : allParams())
            if (getParam (a, p) != getParam (b, p))
                names.addIfNotAlreadyThere (p.coding == CcCoding::ArpNibble ? juce::String ("Arpeggiator")
                                                                             : juce::String (p.section) + " " + p.name);
        return names;
    }

    juce::String shortList (const juce::StringArray& names, int max = 6)
    {
        juce::StringArray shown;
        for (int i = 0; i < names.size() && i < max; ++i)
            shown.add (names[i]);
        if (names.size() > max)
            shown.add (tr ("+%1 more", { juce::String (names.size() - max) }));
        return shown.joinIntoString (", ");
    }

    // Find a device by identifier, falling back to its name (identifiers can change between sessions).
    juce::String resolveDevice (const juce::Array<juce::MidiDeviceInfo>& devices,
                                const juce::String& id, const juce::String& name)
    {
        for (const auto& d : devices)
            if (id.isNotEmpty() && d.identifier == id)
                return d.identifier;
        for (const auto& d : devices)
            if (name.isNotEmpty() && d.name == name)
                return d.identifier;
        return {};
    }

    juce::String deviceName (const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& id)
    {
        for (const auto& d : devices)
            if (d.identifier == id)
                return d.name;
        return {};
    }

    juce::String hostId (const ParamDef& p) { return juce::String ("cc_") + p.id; }

    // The test suite sets this so it never touches the user's settings, ports or backups.
    juce::File testRoot()
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable ("OSCAR_EDITOR_TEST_DIR", {});
        return dir.isNotEmpty() ? juce::File (dir) : juce::File();
    }

    int choiceValue (const ParamDef& p, int index)
    {
        if (p.choices.empty()) return 0;
        return p.choices[(size_t) juce::jlimit (0, (int) p.choices.size() - 1, index)].value;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout OscarProcessor::createHostLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& p : allParams())
    {
        if (p.cc < 0)
            continue;

        const auto name = juce::String (p.section) + " " + p.name;
        if (p.kind == ParamKind::Knob)
        {
            layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { hostId (p), 1 }, name, 0, kKnobMaxEditable, 128));
        }
        else
        {
            juce::StringArray labels;
            for (const auto& c : p.choices)
                labels.add (c.label);
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { hostId (p), 1 }, name, labels, 0));
        }
    }

    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "tempo", 1 }, "Tempo (global)", 1, 254, 128));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "morph", 1 }, "Morph A-B", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    return layout;
}

OscarProcessor::OscarProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      hostParams (*this, nullptr, idHost, createHostLayout())
{
    for (const auto& p : allParams())
        if (p.cc >= 0)
            hostList.push_back ({ hostParams.getRawParameterValue (hostId (p)), &p, -1 });
    hostList.push_back ({ hostParams.getRawParameterValue ("tempo"), nullptr, -1 });

    // Re-open the ports used last time, so a fresh instance is ready to go.
    auto& g = globalSettings();
    if (testRoot() == juce::File())
    {
        midi.openOutput (resolveDevice (juce::MidiOutput::getAvailableDevices(), g.getValue ("outId"), g.getValue ("outName")));
        midi.openInput (resolveDevice (juce::MidiInput::getAvailableDevices(), g.getValue ("inId"), g.getValue ("inName")));
    }
    channel = juce::jlimit (1, 16, g.getIntValue ("channel", 1));

    // French unless chosen otherwise (the test suite runs in English).
    lang::apply (g.getValue ("language", testRoot() != juce::File() ? "en" : "fr"));

    document.addChangeListener (this);
    juce::Timer::startTimer (40);
    juce::HighResolutionTimer::startTimer (2);

    jassert (allParams().size() <= (size_t) kMaxParams);
    randomSentCc.fill (-1);
    publishRandom();

    morphParam = hostParams.getRawParameterValue ("morph");
    morphTicker.tick = [this] { morphTick(); };
    morphTicker.startTimerHz (30);
}

OscarProcessor::~OscarProcessor()
{
    juce::HighResolutionTimer::stopTimer();   // waits for a running callback
    juce::Timer::stopTimer();
    morphTicker.stopTimer();
    document.removeChangeListener (this);
    midi.closeAll();
}

juce::PropertiesFile& OscarProcessor::globalSettings()
{
    if (settingsFile == nullptr && testRoot() != juce::File())
    {
        juce::PropertiesFile::Options o;
        settingsFile = std::make_unique<juce::PropertiesFile> (testRoot().getChildFile ("test.settings"), o);
    }

    if (settingsFile == nullptr)
    {
        const auto options = [] (const char* name)
        {
            juce::PropertiesFile::Options o;
            o.applicationName = name;
            o.filenameSuffix = ".settings";
            o.folderName = name;
            o.osxLibrarySubFolder = "Application Support";
            return o;
        };
        const auto o = options ("OXIde");
        // 0.4 and older kept their settings under the old name: copied once, the old file stays
        const auto file = o.getDefaultFile(), before = options ("OSCar Editor").getDefaultFile();
        if (! file.existsAsFile() && before.existsAsFile() && file.getParentDirectory().createDirectory())
            before.copyFileTo (file);
        settingsFile = std::make_unique<juce::PropertiesFile> (o);
    }
    return *settingsFile;
}

void OscarProcessor::saveGlobalPorts()
{
    auto& g = globalSettings();
    g.setValue ("outId", midi.getOutputId());
    g.setValue ("outName", deviceName (juce::MidiOutput::getAvailableDevices(), midi.getOutputId()));
    g.setValue ("inId", midi.getInputId());
    g.setValue ("inName", deviceName (juce::MidiInput::getAvailableDevices(), midi.getInputId()));
    g.setValue ("channel", channel.load());
    g.saveIfNeeded();
}

//==============================================================================
void OscarProcessor::addLog (const juce::String& text)
{
    for (const auto& line : juce::StringArray::fromLines (text))
        if (line.isNotEmpty())
            log.add (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + line);
    while (log.size() > 200)
        log.remove (0);
    statusBroadcaster.sendChangeMessage();
}

void OscarProcessor::setSelectedSlot (int slot)
{
    slot = juce::jlimit (0, kNumVoices - 1, slot);
    if (slot != selectedSlot)
    {
        selectedSlot = slot;
        publishRandom();
        statusBroadcaster.sendChangeMessage();
    }
}

void OscarProcessor::setChannel (int ch)
{
    channel = juce::jlimit (1, 16, ch);
    saveGlobalPorts();
    statusBroadcaster.sendChangeMessage();
}

void OscarProcessor::setLanguage (const juce::String& code)
{
    lang::apply (code);
    globalSettings().setValue ("language", lang::current());
    globalSettings().saveIfNeeded();
    languageChanged.sendChangeMessage();
}

juce::String OscarProcessor::getLanguage() const
{
    return lang::current();
}

void OscarProcessor::setClockOut (bool b)
{
    clockOut = b;
    statusBroadcaster.sendChangeMessage();
}

void OscarProcessor::setMidiThru (bool b)
{
    midiThru = b;
    statusBroadcaster.sendChangeMessage();
}

void OscarProcessor::setLiveCc (bool b)
{
    if (b && ! liveCc)
        resyncHost = true;   // don't replay automation moves made while it was off
    liveCc = b;
    publishRandom();
    statusBroadcaster.sendChangeMessage();
}

juce::String OscarProcessor::setOutputDevice (const juce::String& identifier)
{
    const auto err = midi.openOutput (identifier);
    saveGlobalPorts();
    if (err.isNotEmpty())
        addLog (err);
    statusBroadcaster.sendChangeMessage();
    return err;
}

juce::String OscarProcessor::setInputDevice (const juce::String& identifier)
{
    const auto err = midi.openInput (identifier);
    saveGlobalPorts();
    if (err.isNotEmpty())
        addLog (err);
    statusBroadcaster.sendChangeMessage();
    return err;
}

//==============================================================================
juce::File OscarProcessor::getBackupFolder()
{
    if (testRoot() != juce::File())
        return testRoot().getChildFile ("Backups");
    return getUserFolder().getChildFile ("Backups");
}

juce::File OscarProcessor::getUserFolder()
{
    if (testRoot() != juce::File())
        return testRoot();
    const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto folder = documents.getChildFile ("OXIde");
    // 0.4 and older: Documents/OSCar Editor. Moved once, with the banks, voices and backups.
    const auto before = documents.getChildFile ("OSCar Editor");
    if (! folder.exists() && before.isDirectory())
        before.moveFileTo (folder);
    return folder;
}

juce::File OscarProcessor::getBanksFolder()
{
    return getUserFolder().getChildFile ("Banks");
}

juce::File OscarProcessor::getVoicesFolder()
{
    return getUserFolder().getChildFile ("Voices");
}

juce::File OscarProcessor::writeBackup (const juce::String& what, const std::vector<std::uint8_t>& bytes)
{
    auto dir = getBackupFolder();
    if (! dir.createDirectory())
        return {};
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    auto f = dir.getNonexistentChildFile (stamp + " " + what, ".syx", false);
    if (! f.replaceWithData (bytes.data(), bytes.size()))
        return {};
    return f;
}

//==============================================================================
// Live CC
//==============================================================================
void OscarProcessor::queueCc (const std::vector<CcMessage>& messages)
{
    if (messages.empty())
        return;

    const juce::ScopedLock sl (outboxLock);
    for (const auto& m : messages)
    {
        // Latest value wins, and moves to the end so a batch keeps its own order
        // (CC 51 must stay before the CC 50 that follows it).
        outbox.erase (std::remove_if (outbox.begin(), outbox.end(),
                                      [&] (const CcMessage& o) { return o.first == m.first; }),
                      outbox.end());
        outbox.push_back (m);
    }
}

void OscarProcessor::editParam (int slot, const ParamDef& p, int value)
{
    // With a scale, Transpose and Osc 2 Interval step from note to note of the scale (from the root;
    // Osc 2 from where Transpose puts oscillator 1), the way the control was turned.
    const auto mask = scales()[(size_t) randomSet.scale].mask;
    if (mask != 0 && isPitchInterval (p))
    {
        const auto& v = document.getVoice (slot);
        const int root = randomSet.root;
        const int ref = std::strcmp (p.id, "osc2Semi") == 0 ? root + semisOfValue (getParam (v, *findParam ("tuneSemi"))) : root;
        value = valueOfSemis (stepInterval (semisOfValue (getParam (v, p)), semisOfValue (value), ref, mask, root, -7, 7));
    }
    document.setParam (slot, p, value);

    if (! liveCc || slot != selectedSlot)
        return;

    if (oscarSound && oscarSlot == slot)
        pushDifferencesToOscar();
    else if (oscarSlot < 0 || oscarSlot == slot)   // never touch another voice the OSCar is playing
        queueCc (ccMessagesFor (p, document.getVoice (slot)));
}

void OscarProcessor::pushDifferencesToOscar()
{
    if (! liveCc || ! oscarSound || oscarSlot != selectedSlot)
        return;

    const auto& v = document.getVoice (selectedSlot);
    if (v == *oscarSound)
        return;

    std::vector<CcMessage> msgs;
    bool arpDone = false;
    for (const auto& p : allParams())
    {
        if (p.cc < 0 || getParam (v, p) == getParam (*oscarSound, p))
            continue;
        if (p.coding == CcCoding::ArpNibble)
        {
            if (arpDone) continue;
            arpDone = true;
        }
        const auto m = ccMessagesFor (p, v);
        msgs.insert (msgs.end(), m.begin(), m.end());
    }

    queueCc (msgs);
    oscarSound = v;
}

void OscarProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    publishRandom();   // the randoms move around the voice as it is now
    // Undo, paste, revert, file loads...: keep what the OSCar plays equal to the editor.
    // (Not while a morph is under way: it paces its own CC.)
    if (morphMoving || morphPending)
        return;
    pushDifferencesToOscar();
}

void OscarProcessor::oscarSelected (int slot)
{
    oscarSlot = slot;
    oscarSound = document.getOscarVoice (slot);   // the OSCar just loaded it from memory
    publishRandom();
    const int token = ++selectionToken;

    // After the OSCar's own CC dump has gone by, make it play the editor's version.
    juce::Timer::callAfterDelay (400, [weak = juce::WeakReference<OscarProcessor> (this), token]
    {
        if (weak != nullptr && weak->selectionToken == token)
            weak->pushDifferencesToOscar();
    });
}

void OscarProcessor::chooseVoice (int slot)
{
    setSelectedSlot (slot);
    if (liveCc && midi.isOutputOpen())
    {
        if (midi.isBusy())
            addLog (tr ("A transfer is still in progress: the OSCar was not switched to voice %1.", { juce::String (slotLabel (slot)) }));
        else if (midi.sendProgramChange (channel, slot))
            oscarSelected (slot);
    }
}

void OscarProcessor::hiResTimerCallback()
{
    RandomShared shared;
    {
        const juce::SpinLock::ScopedLockType sl (randomLock);
        shared = randomShared;
    }
    const auto& scale = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, shared.settings.scale)];
    pumpThru (scale.mask, shared.settings.root);

    // 1. Host automation -> CC (only on change; never on load)
    const bool live = liveCc;
    if (hostSuspend > 0)
        resyncHost = true;
    const bool resync = hostSuspend > 0 || resyncHost.exchange (false);
    std::vector<CcMessage> msgs;
    bool arpChanged = false;

    for (auto& h : hostList)
    {
        const int now = juce::roundToInt (h.raw->load());
        if (now == h.last)
            continue;
        const bool first = h.last < 0;
        h.last = now;
        if (resync || first || ! live)
            continue;

        if (h.def == nullptr)
            msgs.emplace_back (kTempoCc, juce::jlimit (0, 127, now >> 1));
        else if (h.def->coding == CcCoding::ArpNibble)
            arpChanged = true;
        else
        {
            const int value = h.def->kind == ParamKind::Knob ? now : quantizedHostValue (shared, *h.def, choiceValue (*h.def, now));
            const int c = ccValueFor (h.def->coding, value);
            if (c >= 0)
                msgs.emplace_back (h.def->cc, c);
        }
    }

    if (arpChanged)
    {
        int dir = 3, mode = 0, function = 0;
        for (const auto& h : hostList)
        {
            if (h.def == nullptr) continue;
            const int v = choiceValue (*h.def, juce::roundToInt (h.raw->load()));
            if (std::string (h.def->id) == "arpDir")   dir = v;
            if (std::string (h.def->id) == "arpMode")  mode = v;
            if (std::string (h.def->id) == "function") function = v;
        }
        msgs.emplace_back (51, ccValueFor (CcCoding::ArpNibble, (dir & 3) | ((mode & 3) << 2)));
        const int f = ccValueFor (CcCoding::SixSparse, function);
        if (f >= 0)
            msgs.emplace_back (50, f);
    }

    queueCc (msgs);

    // 2. The randoms' step, if one is due
    randomTick (shared, juce::Time::getMillisecondCounterHiRes());

    // 3. Outbox -> OSCar (waits while a SysEx transfer is on the wire)
    const juce::ScopedLock sl (outboxLock);
    if (! outbox.empty() && midi.sendControllers (channel, outbox))
        outbox.clear();
}

//==============================================================================
// A/B morph
//==============================================================================
VoiceData OscarProcessor::blend (const VoiceData& a, const VoiceData& b, float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    VoiceData v = t < 0.5f ? a : b;   // bits no parameter owns come from the nearer voice
    for (const auto& p : allParams())
    {
        const int va = getParam (a, p), vb = getParam (b, p);
        setParam (v, p, p.kind == ParamKind::Knob ? juce::roundToInt ((float) va + (float) (vb - va) * t) : (t < 0.5f ? va : vb));
    }
    return v;
}

void OscarProcessor::setMorphSources (int slotA, int slotB)
{
    morph.slotA = juce::jlimit (0, kNumVoices - 1, slotA);
    morph.slotB = juce::jlimit (0, kNumVoices - 1, slotB);
    morphSession = false;   // the next move blends the new pair
}

void OscarProcessor::loadMorphSource (bool toB, int slot)
{
    slot = juce::jlimit (0, kNumVoices - 1, slot);
    (toB ? morph.slotB : morph.slotA) = slot;
    if (! morphSession || morphSlot != selectedSlot)
        return;   // nothing blended yet: the next move of the fader starts from the new pair
    (toB ? morphB : morphA) = document.getVoice (slot);
    morphFrom = document.getVoice (morphSlot);   // what is heard now
    morphFade = 0.0f;
}

void OscarProcessor::morphKeep()
{
    if (morphSession)
        addLog (tr ("Morph kept in voice %1.", { juce::String (slotLabel (morphSlot)) }));
    morphSession = false;
    document.endUndoGroup();
}

void OscarProcessor::startMorphSession()
{
    morphSlot = selectedSlot;
    const auto current = document.getVoice (morphSlot);   // the slot's own sound, when it is A or B
    morphA = morph.slotA == morphSlot ? current : document.getVoice (morph.slotA);
    morphB = morph.slotB == morphSlot ? current : document.getVoice (morph.slotB);
    morphSent = (oscarSound && oscarSlot == morphSlot) ? *oscarSound : current;   // what the OSCar plays
    morphFade = 1.0f;
    morphSession = true;
    document.endUndoGroup();   // a new blend is a new undo step
}

void OscarProcessor::morphTick()
{
    if (morphParam == nullptr)
        return;
    const float target = juce::jlimit (0.0f, 1.0f, morphParam->load());
    if (! morphSeen)   // a project being loaded moves nothing
    {
        morphSeen = true;
        morphTarget = morphPos = target;
        return;
    }
    if (target != morphTarget)
    {
        morphTarget = target;
        morphMoving = true;
    }
    const bool fading = morphSession && morphSlot == selectedSlot && morphFade < 1.0f;
    if (! morphMoving && ! fading)
    {
        if (morphPending)
            morphPending = flushMorphCc();
        return;
    }

    if (! morphSession || morphSlot != selectedSlot)
        startMorphSession();
    const float maxStep = morph.smoothing <= 0.01f ? 1.0f : (1.0f / 30.0f) / morph.smoothing;
    if (morphMoving)
    {
        morphPos += juce::jlimit (-maxStep, maxStep, morphTarget - morphPos);
        if (std::abs (morphTarget - morphPos) < 1.0e-4f)
        {
            morphPos = morphTarget;
            morphMoving = false;
        }
    }
    auto v = blend (morphA, morphB, morphPos);
    if (morphFade < 1.0f)   // a voice just loaded at one end: glide there from what was heard
    {
        morphFade = juce::jmin (1.0f, morphFade + maxStep);
        v = blend (morphFrom, v, morphFade);
    }
    applyMorph (v);
}

void OscarProcessor::applyMorph (const VoiceData& v)
{
    if (v != document.getVoice (morphSlot))
        document.setVoiceInGroup (morphSlot, v, "morph");   // one undo step for the whole blend, pauses included
    morphPending = flushMorphCc();
}

bool OscarProcessor::flushMorphCc()
{
    const int slot = morphSlot;
    if (! liveCc || slot != selectedSlot || (oscarSlot >= 0 && oscarSlot != slot))   // never touch another voice the OSCar is playing
        return false;
    if (! midi.isOutputOpen() || midi.isBusy())
        return true;   // after the transfer

    // what the OSCar still lacks, the biggest moves first
    const auto& v = document.getVoice (slot);
    struct Pending { std::vector<const ParamDef*> params; std::vector<CcMessage> msgs; int size; };
    std::vector<Pending> pending;
    bool arpDone = false;
    for (const auto& p : allParams())
    {
        if (p.cc < 0)
            continue;
        Pending e;
        if (p.coding == CcCoding::ArpNibble)   // direction and mode share one CC
        {
            if (arpDone) continue;
            arpDone = true;
            for (const auto& q : allParams())
                if (q.coding == CcCoding::ArpNibble)
                    e.params.push_back (&q);
        }
        else
        {
            e.params.push_back (&p);
        }
        e.msgs = ccMessagesFor (p, v);
        const auto before = ccMessagesFor (p, morphSent);
        if (e.msgs == before)
            continue;
        e.size = (e.msgs.empty() || before.empty()) ? 127 : std::abs (e.msgs.front().second - before.front().second);
        pending.push_back (std::move (e));
    }
    std::stable_sort (pending.begin(), pending.end(), [] (const Pending& a, const Pending& b) { return a.size > b.size; });

    std::vector<CcMessage> out;
    size_t done = 0;
    for (const auto& e : pending)
    {
        if ((int) (out.size() + e.msgs.size()) > morphCcPerTick)
            break;
        out.insert (out.end(), e.msgs.begin(), e.msgs.end());
        for (auto* q : e.params)
            setParam (morphSent, *q, getParam (v, *q));
        ++done;
    }
    queueCc (out);
    if (oscarSlot == slot)
        oscarSound = morphSent;
    return done < pending.size();
}

//==============================================================================
// Generator
//==============================================================================
oscar::Corpus OscarProcessor::learningCorpus() const
{
    // Learn from the factory bank, the user's bank and what the OSCar holds.
    std::vector<VoiceData> extra (document.getBank().voices.begin(), document.getBank().voices.end());
    if (const auto& known = document.getOscarBank())
        extra.insert (extra.end(), known->voices.begin(), known->voices.end());
    return Corpus::withFactory (extra);
}

void OscarProcessor::applyToSelected (const VoiceData& v, const juce::String& undoKey)
{
    const int slot = selectedSlot;
    document.setVoice (slot, v, undoKey);

    // Audition: make the OSCar play it now. When we don't know what it is playing,
    // send the whole voice rather than the differences.
    if (liveCc && (oscarSlot == slot || oscarSlot < 0))
    {
        if (oscarSound && oscarSlot == slot)
        {
            pushDifferencesToOscar();
        }
        else
        {
            std::vector<CcMessage> msgs;
            bool arpDone = false;
            for (const auto& p : allParams())
            {
                if (p.cc < 0) continue;
                if (p.coding == CcCoding::ArpNibble) { if (arpDone) continue; arpDone = true; }
                const auto m = ccMessagesFor (p, v);
                msgs.insert (msgs.end(), m.begin(), m.end());
            }
            queueCc (msgs);
            if (oscarSlot == slot)
                oscarSound = v;
        }
    }
}

void OscarProcessor::generate()
{
    const int slot = selectedSlot;
    if (genSlot != slot || genHistory.empty())
    {
        genHistory.clear();
        genHistory.push_back ({ document.getVoice (slot), tr ("ORIGINAL") + juce::String::fromUTF8 (" \xc2\xb7 ") + tr ("VOICE %1", { juce::String (slotLabel (slot)) }), juce::Time::getCurrentTime() });
        genSlot = slot;
        genIndex = 0;
    }

    const auto& base = document.getVoice (slot);
    const auto corpus = learningCorpus();
    Generator g ((std::uint32_t) juce::Random::getSystemRandom().nextInt());
    VoiceData out;
    juce::String label;

    switch (gen.mode)
    {
        case GenMode::Vary:
            out = g.vary (base, gen.amount, gen.locks, corpus);
            label = tr ("VARY") + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (juce::roundToInt (gen.amount * 100.0)) + " %";
            break;
        case GenMode::New:
            out = g.create (base, gen.family, gen.locks, corpus);
            label = tr ("NEW") + juce::String::fromUTF8 (" \xc2\xb7 ") + lang::upper (tr (familyName (gen.family)));
            break;
        case GenMode::Breed:
        {
            const int a = juce::jlimit (0, kNumVoices - 1, gen.parentA), b = juce::jlimit (0, kNumVoices - 1, gen.parentB);
            // the slot's own sound is parent A when A is the slot being generated into
            const auto& pa = (a == slot) ? genHistory.front().voice : document.getVoice (a);
            const auto& pb = (b == slot) ? genHistory.front().voice : document.getVoice (b);
            out = g.breed (pa, pb, gen.amount * 0.5, gen.locks, corpus);
            label = tr ("BREED") + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (slotLabel (a)) + " x " + juce::String (slotLabel (b));
            break;
        }
    }

    // a new result drops the "future" if we had stepped back
    genHistory.resize ((size_t) genIndex + 1);
    genHistory.push_back ({ out, juce::String (genHistory.size()).paddedLeft ('0', 2) + "  " + label, juce::Time::getCurrentTime() });
    genIndex = (int) genHistory.size() - 1;
    applyToSelected (out, "generator");
    addLog (liveCc ? tr ("Generated (%1) into voice %2: playing on the OSCar, not stored in its memory yet.", { label.toLowerCase(), juce::String (slotLabel (slot)) })
                   : tr ("Generated (%1) into voice %2.", { label.toLowerCase(), juce::String (slotLabel (slot)) }));
}

void OscarProcessor::genStep (int delta)
{
    if (genHistory.empty() || genSlot != selectedSlot)
        return;
    const int i = juce::jlimit (0, (int) genHistory.size() - 1, genIndex + delta);
    if (i == genIndex)
        return;
    genIndex = i;
    applyToSelected (genHistory[(size_t) i].voice, "generator");
    statusBroadcaster.sendChangeMessage();
}

void OscarProcessor::genBackToOriginal()
{
    if (genHistory.empty() || genSlot != selectedSlot)
        return;
    genIndex = 0;
    applyToSelected (genHistory.front().voice, "generator");
    addLog (tr ("Voice %1 is back to its original sound.", { juce::String (slotLabel (genSlot)) }));
}

void OscarProcessor::genKeep()
{
    if (genHistory.empty())
        return;
    addLog (tr ("Kept %1 in voice %2. \"Send voices to synth\" stores it in the OSCar.",
                { genHistory[(size_t) genIndex].label.fromFirstOccurrenceOf ("  ", false, false).toLowerCase(), juce::String (slotLabel (genSlot)) }));
    genHistory.clear();
    genIndex = -1;
    genSlot = -1;
    statusBroadcaster.sendChangeMessage();
}

//==============================================================================
// MIDI thru (CC engine thread)
//==============================================================================
void OscarProcessor::pumpThru (std::uint16_t scaleMask, int scaleRoot)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool busy = midi.isBusy();
    const bool on = midiThru;
    const int ch = channel;
    std::vector<juce::MidiMessage> out;
    std::vector<int> outFrom;   // the note received behind each note sent (-1: not a note)

    // A received note is released: the note sent for it goes off once no other received note holds
    // it (with a scale, two keys can give the same note).
    auto release = [&] (int n)
    {
        thruActive[(size_t) n] = false;
        thruPendingOff[(size_t) n] = false;
        auto& count = thruOutCount[(size_t) thruMap[(size_t) n]];
        if (count > 0 && --count == 0)
        {
            out.push_back (juce::MidiMessage::noteOff (ch, thruMap[(size_t) n]));
            outFrom.push_back (n);
        }
    };

    if (! busy)
    {
        for (int n = 0; n < 128; ++n)
        {
            // note-offs held back during a transfer, or notes still on when thru was turned off
            if (thruPendingOff[(size_t) n] || (! on && thruActive[(size_t) n]))
                release (n);
        }
        if (! on && thruBent)
        {
            out.push_back (juce::MidiMessage::pitchWheel (ch, 8192));
            outFrom.push_back (-1);
            thruBent = false;
        }
    }

    int s1, n1, s2, n2;
    thruFifo.prepareToRead (thruFifo.getNumReady(), s1, n1, s2, n2);

    auto handle = [&] (const std::array<std::uint8_t, 3>& d)
    {
        if (d[0] >= 0xF8)                           // clock tick, Start, Continue, Stop
        {
            if (! busy)
            {
                out.push_back (juce::MidiMessage ((int) d[0]));
                outFrom.push_back (-1);
            }
            return;
        }
        if (! on)
            return;
        const int status = d[0] & 0xF0, note = d[1] & 0x7F, vel = d[2] & 0x7F;

        if (status == 0x90 && vel > 0)
        {
            if (now - midi.lastNoteOnFromOscar (note) < kEchoWindowMs)
            {
                thruEchoed[(size_t) note] = true;   // its note-off is an echo too
                return;
            }
            if (busy)
                return;                             // not during a transfer
            thruEchoed[(size_t) note] = false;
            if (thruActive[(size_t) note])
                release (note);                     // a second note-on without its note-off
            const int sent = juce::jlimit (0, 127, quantizeNote (note, scaleMask, scaleRoot));
            thruMap[(size_t) note] = sent;
            thruActive[(size_t) note] = true;
            ++thruOutCount[(size_t) sent];
            thruLastNote = sent;
            out.push_back (juce::MidiMessage::noteOn (ch, sent, (juce::uint8) vel));
            outFrom.push_back (note);
        }
        else if (status == 0x80 || status == 0x90)
        {
            if (thruEchoed[(size_t) note])
            {
                thruEchoed[(size_t) note] = false;
                return;
            }
            if (! thruActive[(size_t) note])
                return;
            if (busy)
            {
                thruPendingOff[(size_t) note] = true;
                return;
            }
            release (note);
        }
        else if (status == 0xE0 && ! busy)
        {
            const int value = (d[2] << 7) | d[1];
            thruBent = value != 8192;
            out.push_back (juce::MidiMessage::pitchWheel (ch, value));
            outFrom.push_back (-1);
        }
    };

    // events are sent when they are due (they carry their position in the audio block)
    int consumed = 0;
    for (int part = 0; part < 2; ++part)
    {
        const int start = part == 0 ? s1 : s2, count = part == 0 ? n1 : n2;
        int i = 0;
        for (; i < count; ++i)
        {
            const auto& e = thruData[(size_t) (start + i)];
            if (e.when > now + 0.5)
                break;
            handle (e.data);
            ++consumed;
        }
        if (i < count)
            break;
    }
    thruFifo.finishedRead (consumed);

    if (out.empty() || midi.sendShortMessages (out))
        return;

    // A transfer started meanwhile (or the port is closed): never leave a note hanging.
    for (size_t i = 0; i < out.size(); ++i)
    {
        const int n = outFrom[i];
        if (n < 0)
            continue;
        auto& count = thruOutCount[(size_t) thruMap[(size_t) n]];
        if (out[i].isNoteOn())
        {
            thruActive[(size_t) n] = false;
            if (count > 0) --count;
        }
        else if (out[i].isNoteOff() && midi.isOutputOpen())
        {
            thruActive[(size_t) n] = true;     // released again once the transfer is over
            thruPendingOff[(size_t) n] = true;
            ++count;
        }
    }
}

//==============================================================================
// Transfers
//==============================================================================
juce::String OscarProcessor::sendVoices (bool selectSlotAfter)
{
    if (! midi.isOutputOpen())
        return tr ("No MIDI output selected.");
    if (midi.isBusy())
        return tr ("A transfer is still in progress.");

    // What the OSCar holds is about to be overwritten: keep a copy when we know it.
    if (const auto& known = document.getOscarBank(); known && *known != document.getBank())
        writeBackup ("OSCar voices before send", buildVoiceMessage (*known));

    const auto msg = buildVoiceMessage (document.getBank());
    const auto backup = writeBackup ("sent voices", msg);

    if (! midi.sendSysex (msg))
        return tr ("The MIDI output refused the data.");

    document.markVoicesSent();
    addLog (tr ("Sent 36 voices (%1 bytes, about %2 s).", { juce::String ((int) msg.size()), juce::String (transferSeconds (msg.size()), 1) })
            + (backup.existsAsFile() ? juce::String() : " " + tr ("Warning: backup file could not be written.")));

    if (selectSlotAfter)
    {
        const int slot = selectedSlot;
        const int delay = (int) midi.busyRemainingMs() + 300;
        juce::Timer::callAfterDelay (delay, [weak = juce::WeakReference<OscarProcessor> (this), slot]
        {
            if (weak != nullptr)
            {
                const auto err = weak->selectOnOscar (slot);
                if (err.isNotEmpty())
                    weak->addLog (err);
            }
        });
    }
    return {};
}

juce::String OscarProcessor::sendWaves()
{
    if (! document.getWaves())
        return tr ("No waveform data loaded yet: dump the waveforms from the OSCar first.");
    if (! midi.isOutputOpen())
        return tr ("No MIDI output selected.");
    if (midi.isBusy())
        return tr ("A transfer is still in progress.");

    const auto msg = buildWaveMessage (*document.getWaves());
    const auto backup = writeBackup ("sent waveforms", msg);
    if (! midi.sendSysex (msg))
        return tr ("The MIDI output refused the data.");

    document.markWavesSent();
    addLog (tr ("Sent waveforms -8 to -12 (%1 bytes).", { juce::String ((int) msg.size()) })
            + (backup.existsAsFile() ? juce::String() : " " + tr ("Warning: backup file could not be written.")));
    return {};
}

juce::String OscarProcessor::sendSequences()
{
    if (! document.getSequences() || document.getSequences()->empty())
        return tr ("No sequence data loaded.");
    if (! midi.isOutputOpen())
        return tr ("No MIDI output selected.");
    if (midi.isBusy())
        return tr ("A transfer is still in progress.");

    const auto msg = buildSequenceMessage (*document.getSequences());
    writeBackup ("sent sequences", msg);
    if (! midi.sendSysex (msg))
        return tr ("The MIDI output refused the data.");

    addLog (tr ("Sent sequences (%1 bytes, about %2 s).", { juce::String ((int) msg.size()), juce::String (transferSeconds (msg.size()), 1) }));
    return {};
}

juce::String OscarProcessor::selectOnOscar (int slot)
{
    if (! midi.isOutputOpen())
        return tr ("No MIDI output selected.");
    if (slot < 0 || slot >= kNumVoices)
        return tr ("No voice selected.");
    if (midi.isBusy())
        return tr ("A transfer is still in progress.");
    if (! midi.sendProgramChange (channel, slot))
        return tr ("The MIDI output refused the program change.");
    oscarSelected (slot);
    addLog (tr ("Program change %1 sent (voice %2).", { juce::String (slot), juce::String (slotLabel (slot)) }));
    return {};
}

//==============================================================================
// Incoming
//==============================================================================
void OscarProcessor::sysexReceived (const std::vector<std::uint8_t>& message)
{
    if (message.size() < 3 || message[1] != kManufacturerId)
        return; // another device's SysEx: not ours

    DumpContents d;
    std::string err;
    if (! parseStream (message, d, err))
    {
        writeBackup ("received (unreadable)", message);
        addLog (tr ("Received OSCar data could not be read: %1 Nothing was changed. Try the dump again.", { tr (juce::String (err)) }));
        return;
    }

    juce::StringArray kinds;
    if (d.voices)    kinds.add ("voices");
    if (d.waves)     kinds.add ("waveforms");
    if (d.sequences) kinds.add ("sequences");
    const auto backup = writeBackup ("received " + kinds.joinIntoString (" "), message);

    // The OSCar's current sound may differ from its memory (panel moves): stop assuming.
    if (d.voices)
        oscarSound.reset();

    addLog (document.receiveDump (d)
            + (backup.existsAsFile() ? juce::String() : "\n" + tr ("Warning: backup file could not be written.")));
}

void OscarProcessor::programChangeReceived (int, int program)
{
    if (program < 0 || program >= kNumVoices)
        return;
    if (followOscar)
        setSelectedSlot (program);
    oscarSelected (program);
    awaitingDumpSince = juce::Time::getMillisecondCounterHiRes();   // a Tauntek with CC dump on answers within ~0.2 s
}

void OscarProcessor::controllerReceived (int, int cc, int value)
{
    if (cc < 14 || cc > 55)
        return;

    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastDumpCcMs > 300.0)
    {
        std::fill (dumpValues.begin(), dumpValues.end(), -1);
        dumpCount = 0;
    }
    dumpValues[(size_t) cc] = value;
    ++dumpCount;
    lastDumpCcMs = now;
}

void OscarProcessor::timerCallback()
{
    publishRandom();   // whatever changed meanwhile (cheap: a copy of the voice and the amounts)

    // the OSCar on another MIDI channel: say so once, as soon as it sends something
    if (const int ch = midi.lastChannelFromOscar(); ch > 0 && ch != oscarChannel)
    {
        oscarChannel = ch;
        if (ch != channel)
            addLog (tr ("The OSCar sends on MIDI channel %1 and the plugin on channel %2: unless it is in omni mode, the OSCar doesn't hear the plugin. On the OSCar, hold SPACE and press key %2; or choose channel %1 on the Setup page.",
                        { juce::String (ch), juce::String (channel.load()) }));
        statusBroadcaster.sendChangeMessage();
    }

    if (awaitingDumpSince > 0 && juce::Time::getMillisecondCounterHiRes() - awaitingDumpSince > 1500.0)
    {
        awaitingDumpSince = 0;
        if (++ccDumpMisses >= 2 && ccDumpState != CcDumpState::Missing)
        {
            ccDumpState = CcDumpState::Missing;
            statusBroadcaster.sendChangeMessage();
        }
    }

    // A Tauntek patch dump is a burst of ~41 CCs; evaluate it once the burst is over.
    if (dumpCount > 0 && juce::Time::getMillisecondCounterHiRes() - lastDumpCcMs > 120.0)
    {
        if (dumpCount >= 30)
            evaluateCcDump();
        std::fill (dumpValues.begin(), dumpValues.end(), -1);
        dumpCount = 0;
    }
}

void OscarProcessor::evaluateCcDump()
{
    awaitingDumpSince = 0;
    ccDumpMisses = 0;
    if (ccDumpState != CcDumpState::Active)
    {
        ccDumpState = CcDumpState::Active;
        statusBroadcaster.sendChangeMessage();
    }

    if (oscarSlot < 0)
    {
        addLog (tr ("Patch CC dump received, but the plugin doesn't know which voice is playing (turn on program change on the OSCar: SPACE + key -9)."));
        return;
    }

    // Total recall: rebuild what the OSCar is playing. What we know of its memory is the
    // best reference for the knobs' lowest bit; otherwise the editor's copy.
    const int slot = oscarSlot;
    const auto voice = juce::String (slotLabel (slot));
    const auto known = document.getOscarVoice (slot);
    const auto editor = document.getVoice (slot);
    const auto recall = voiceFromCcDump (known ? *known : editor, dumpValues);
    oscarSound = recall.voice;

    if (! recall.unreadable.empty())
    {
        juce::StringArray names;
        for (const auto& n : recall.unreadable) names.add (juce::String (n));
        addLog (tr ("Voice %1: unexpected values in the OSCar's dump for %2 (left unchanged).", { voice, shortList (names) }));
    }

    // Edits made in the plugin and not sent yet win: the OSCar is made to play them.
    if (document.hasUnsentEdits (slot))
    {
        document.oscarReportedVoice (slot, recall.voice, false);
        if (liveCc && slot == selectedSlot)
        {
            pushDifferencesToOscar();
            addLog (tr ("Voice %1 has edits not sent to the OSCar's memory yet: the OSCar now plays the plugin's version.", { voice }));
        }
        else
        {
            addLog (liveCc ? tr ("Voice %1 has edits not sent to the OSCar's memory yet: the OSCar plays its stored version.", { voice })
                           : tr ("Voice %1 has edits not sent to the OSCar's memory yet: the OSCar plays its stored version (Live CC is off).", { voice }));
        }
        return;
    }

    // Otherwise the plugin shows what the OSCar plays.
    const auto diff = differingParams (editor, recall.voice);
    document.oscarReportedVoice (slot, recall.voice, true);
    if (diff.isEmpty())
        addLog (tr ("Voice %1 on the OSCar matches the plugin (OK).", { voice }));
    else
        addLog (tr ("Voice %1 updated from the OSCar: %2. Undo to go back.", { voice, shortList (diff) }));
}

void OscarProcessor::receiveProgress (int bytes)
{
    receiveBytes = bytes;
    statusBroadcaster.sendChangeMessage();
}

//==============================================================================
bool OscarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void OscarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Silent: the plugin only talks to the OSCar through its own MIDI ports.
    buffer.clear();

    if (isNonRealtime())   // offline bounce: nothing goes to the hardware
    {
        midiMessages.clear();
        clockRunning = false;
        return;
    }

    // Real-time safe: no locks, no allocation. Events are collected for this block, put in
    // time order, then queued; the CC engine thread sends each one when it is due.
    const double sr = sampleRate.load();
    const int numSamples = buffer.getNumSamples();
    const double blockMs = 1000.0 * numSamples / sr;

    // Where this block sits on the system clock. Hosts call processBlock at slightly irregular
    // moments, so the expected start (previous start + previous length) is only pulled gently
    // towards the call time; this keeps the clock ticks evenly spaced.
    const double wall = juce::Time::getMillisecondCounterHiRes();
    double blockStart = predictedBlockMs + 0.05 * (wall - predictedBlockMs);
    if (predictedBlockMs <= 0 || std::abs (wall - predictedBlockMs) > 50.0)
        blockStart = wall;
    predictedBlockMs = blockStart + blockMs;

    std::array<OutEvent, 512> events;
    size_t count = 0;
    auto add = [&] (std::uint8_t a, std::uint8_t b, std::uint8_t c, int sampleOffset)
    {
        if (count < events.size())
            events[count++] = { { a, b, c }, blockStart + 1000.0 * juce::jlimit (0, juce::jmax (0, numSamples - 1), sampleOffset) / sr };
    };

    // MIDI thru: notes and pitch bend only (a stray CC must not change the sound behind the editor's back)
    if (midiThru)
    {
        for (const auto meta : midiMessages)
        {
            if (meta.numBytes != 3)
                continue;
            const auto status = meta.data[0] & 0xF0;
            if (status == 0x80 || status == 0x90 || status == 0xE0)
                add (meta.data[0], meta.data[1], meta.data[2], meta.samplePosition);
        }
    }
    midiMessages.clear();

    // The host's tempo and transport: for the MIDI clock, and the randoms' steps
    bool playing = false, hasTempo = false;
    double bpm = 120.0, ppq = 0.0;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto b = pos->getBpm(); b.hasValue() && *b > 0.0)
            {
                bpm = juce::jlimit (20.0, 400.0, *b);
                hasTempo = true;
            }
            if (pos->getIsPlaying() && pos->getPpqPosition().hasValue())
            {
                playing = true;
                ppq = *pos->getPpqPosition();
            }
        }
    if (hasTempo)
    {
        const juce::SpinLock::ScopedTryLockType tl (hostTimeLock);   // never waits on the audio thread
        if (tl.isLocked())
            hostTime = { bpm, ppq, blockStart, playing };
        hostTempoSeen = bpm;
    }
    playing = playing && clockOut;   // from here on: the MIDI clock

    if (playing)
    {
        const double ticksPerSample = bpm / 60.0 * 24.0 / sr;
        const double tick0 = ppq * 24.0;
        if (! clockRunning || std::abs (nextClockTick - tick0) > 1.5)   // start, loop or jump
            nextClockTick = std::ceil (tick0 - 1.0e-6);
        if (! clockRunning)
            add (ppq < 0.001 ? 0xFA : 0xFB, 0, 0, 0);    // Start from the top, otherwise Continue
        const double tick1 = tick0 + numSamples * ticksPerSample;
        for (; nextClockTick < tick1; nextClockTick += 1.0)
            add (0xF8, 0, 0, (int) ((nextClockTick - tick0) / ticksPerSample));
        clockRunning = true;
    }
    else if (clockRunning)
    {
        add (0xFC, 0, 0, 0);   // Stop
        clockRunning = false;
    }

    std::stable_sort (events.begin(), events.begin() + (std::ptrdiff_t) count,
                      [] (const OutEvent& x, const OutEvent& y) { return x.when < y.when; });

    int s1, n1, s2, n2;
    thruFifo.prepareToWrite ((int) count, s1, n1, s2, n2);   // when full, the rest is dropped
    for (int i = 0; i < n1 + n2; ++i)
        thruData[(size_t) (i < n1 ? s1 + i : s2 + i - n1)] = events[(size_t) i];
    thruFifo.finishedWrite (n1 + n2);
}

juce::AudioProcessorEditor* OscarProcessor::createEditor()
{
    return new OscarEditor (*this);
}

//==============================================================================
void OscarProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree s (idState);
    s.appendChild (document.toValueTree(), nullptr);
    s.appendChild (hostParams.copyState(), nullptr);
    s.setProperty (idOutId, midi.getOutputId(), nullptr);
    s.setProperty (idOutName, deviceName (juce::MidiOutput::getAvailableDevices(), midi.getOutputId()), nullptr);
    s.setProperty (idInId, midi.getInputId(), nullptr);
    s.setProperty (idInName, deviceName (juce::MidiInput::getAvailableDevices(), midi.getInputId()), nullptr);
    s.setProperty (idChannel, channel.load(), nullptr);
    s.setProperty (idSlot, selectedSlot, nullptr);
    s.setProperty (idFollow, followOscar, nullptr);
    s.setProperty (idSelectAfter, selectAfterSend, nullptr);
    s.setProperty (idLiveCc, liveCc.load(), nullptr);
    s.setProperty (idThru, midiThru.load(), nullptr);
    s.setProperty (idClock, clockOut.load(), nullptr);
    saveRandomState (s);

    if (auto xml = s.createXml())
        copyXmlToBinary (*xml, destData);
}

void OscarProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (idState))
        return;

    if (juce::MessageManager::existsAndIsCurrentThread())
        applyState (state);
    else
        juce::MessageManager::callAsync ([weak = juce::WeakReference<OscarProcessor> (this), state]
        {
            if (weak != nullptr)
                weak->applyState (state);
        });
}

void OscarProcessor::applyState (const juce::ValueTree& s)
{
    document.fromValueTree (s.getChildWithName ("OSCarBank"));

    // Restoring automation values must not send anything.
    const auto host = s.getChildWithName (idHost);
    if (host.isValid())
    {
        ++hostSuspend;
        resyncHost = true;
        hostParams.replaceState (host);
        resyncHost = true;
        --hostSuspend;
    }

    const auto outId = resolveDevice (juce::MidiOutput::getAvailableDevices(),
                                      s.getProperty (idOutId).toString(), s.getProperty (idOutName).toString());
    const auto inId = resolveDevice (juce::MidiInput::getAvailableDevices(),
                                     s.getProperty (idInId).toString(), s.getProperty (idInName).toString());
    if (testRoot() == juce::File())
    {
        if (outId.isNotEmpty() && outId != midi.getOutputId()) midi.openOutput (outId);
        if (inId.isNotEmpty() && inId != midi.getInputId())    midi.openInput (inId);
    }

    channel = juce::jlimit (1, 16, (int) s.getProperty (idChannel, 1));
    selectedSlot = juce::jlimit (0, kNumVoices - 1, (int) s.getProperty (idSlot, 0));
    followOscar = (bool) s.getProperty (idFollow, true);
    selectAfterSend = (bool) s.getProperty (idSelectAfter, true);
    liveCc = (bool) s.getProperty (idLiveCc, true);
    midiThru = (bool) s.getProperty (idThru, false);
    clockOut = (bool) s.getProperty (idClock, false);
    loadRandomState (s.getChildWithName (idRandom));

    // A reloaded project says nothing about what the OSCar is playing now.
    oscarSlot = -1;
    oscarSound.reset();
    statusBroadcaster.sendChangeMessage();
}

//==============================================================================
// Random and scale
//==============================================================================
namespace
{
    int indexOfParam (const ParamDef& p)
    {
        const auto& all = allParams();
        for (size_t i = 0; i < all.size(); ++i)
            if (&all[i] == &p || std::strcmp (all[i].id, p.id) == 0)
                return (int) i;
        return -1;
    }
}

void OscarProcessor::publishRandom()
{
    RandomShared s;
    s.settings = randomSet;
    s.amounts = randomAmounts;
    s.seeds = randomSeeds;
    s.base = document.getVoice (selectedSlot);
    s.live = liveCc;
    s.plays = oscarSlot < 0 || oscarSlot == selectedSlot;   // never touch another voice the OSCar is playing
    const juce::SpinLock::ScopedLockType sl (randomLock);
    randomShared = s;
}

void OscarProcessor::setRandomShape (int index)
{
    randomSet.shape = juce::jlimit (0, (int) modShapes().size() - 1, index);
    publishRandom();
}

void OscarProcessor::setRandomRate (int index)
{
    randomSet.rate = juce::jlimit (0, (int) randomRates().size() - 1, index);
    publishRandom();
}

void OscarProcessor::setRandomLoop (int steps)
{
    randomSet.loop = juce::jlimit (1, 256, steps);
    publishRandom();
}

void OscarProcessor::setRandomBpm (int bpm)
{
    randomSet.bpm = juce::jlimit (20, 400, bpm);
    publishRandom();
}

void OscarProcessor::setScale (int index)
{
    randomSet.scale = juce::jlimit (0, (int) scales().size() - 1, index);
    publishRandom();
}

void OscarProcessor::setScaleRoot (int root)
{
    randomSet.root = ((root % 12) + 12) % 12;
    publishRandom();
}

int OscarProcessor::getRandomAmount (const ParamDef& p) const
{
    const int i = indexOfParam (p);
    return i >= 0 ? randomAmounts[(size_t) i] : 0;
}

void OscarProcessor::setRandomAmount (const ParamDef& p, int amount)
{
    const int i = indexOfParam (p);
    if (i < 0 || ! isRandomisable (p))
        return;
    amount = juce::jlimit (-kRandomMax, kRandomMax, amount);
    auto& a = randomAmounts[(size_t) i];
    if (a == amount)
        return;
    if (a != 0 && amount == 0)
        ++randomSeeds[(size_t) i];   // MIFM1: back to the centre, another loop next time
    a = amount;
    publishRandom();
}

bool OscarProcessor::hasRandoms() const
{
    return std::any_of (randomAmounts.begin(), randomAmounts.end(), [] (int a) { return a != 0; });
}

void OscarProcessor::clearRandoms()
{
    for (size_t i = 0; i < randomAmounts.size(); ++i)
        if (randomAmounts[i] != 0)
        {
            randomAmounts[i] = 0;
            ++randomSeeds[i];
        }
    publishRandom();
}

void OscarProcessor::newRandomLoops()
{
    for (auto& seed : randomSeeds)
        ++seed;
    publishRandom();
}

int OscarProcessor::quantizedHostValue (const RandomShared& s, const ParamDef& p, int value) const
{
    // DAW automation of Transpose or Osc 2 Interval: the nearest note of the scale
    const auto mask = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, s.settings.scale)].mask;
    if (mask == 0 || ! isPitchInterval (p))
        return value;
    const int root = s.settings.root;
    const int ref = std::strcmp (p.id, "osc2Semi") == 0 ? root + semisOfValue (getParam (s.base, *findParam ("tuneSemi"))) : root;
    return valueOfSemis (nearestInterval (semisOfValue (value), ref, mask, root, -7, 7));
}

void OscarProcessor::randomTick (const RandomShared& s, double now)
{
    // The clock: the DAW's position while it plays (the loops start on its bars); otherwise the
    // steps go on at its last tempo, or at the BPM setting when no DAW gives one (standalone).
    HostTime h;
    {
        const juce::SpinLock::ScopedLockType sl (hostTimeLock);
        h = hostTime;
    }
    const double bpm = juce::jlimit (20.0, 400.0, h.bpm > 0.0 ? h.bpm : (double) s.settings.bpm);
    const double dt = randomLastMs > 0.0 ? juce::jlimit (0.0, 100.0, now - randomLastMs) : 0.0;
    randomLastMs = now;
    if (h.playing && h.at > 0.0 && now - h.at < 250.0)
        randomBeats = h.ppq + juce::jmax (0.0, now - h.at) * bpm / 60000.0;
    else
        randomBeats += dt * bpm / 60000.0;
    const auto& rates = randomRates();
    const double stepBeats = rates[(size_t) juce::jlimit (0, (int) rates.size() - 1, s.settings.rate)].beats;
    const auto step = (long long) std::floor (randomBeats / stepBeats + 1.0e-7);

    const auto& params = allParams();
    const size_t n = params.size();
    bool any = false;
    for (size_t i = 0; i < n; ++i)
        any = any || s.amounts[i] != 0;

    std::vector<CcMessage> msgs;
    auto put = [&msgs, &params] (size_t i, const VoiceData& v)
    {
        const auto m = ccMessagesFor (params[i], v);
        msgs.insert (msgs.end(), m.begin(), m.end());
    };

    if (! (any && s.live && s.plays))
    {
        // stopped (no random left, Live CC off, the OSCar on another voice): the OSCar gets the
        // voice's own values back, unless it plays another voice now
        if (randomPlaying.exchange (false) && s.plays)
            for (size_t i = 0; i < n; ++i)
                if (randomLive[i])
                    put (i, s.base);
        randomLive.fill (false);
        randomSentCc.fill (-1);
        randomLastStep = step;
        queueCc (msgs);
        return;
    }
    randomPlaying = true;

    for (size_t i = 0; i < n; ++i)   // a random back to 0: the control's own value
        if (randomLive[i] && s.amounts[i] == 0)
        {
            put (i, s.base);
            randomLive[i] = false;
            randomSentCc[i] = -1;
        }

    const bool newStep = step != randomLastStep;
    randomLastStep = step;
    if (newStep)
        ++randomSteps;   // a step, or a wave's cycle (the RANDOM light)
    randomTokens = juce::jmin (12.0, randomTokens + dt * 0.4);

    const int shape = juce::jlimit (0, (int) modShapes().size() - 1, s.settings.shape);
    const auto mask = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, s.settings.scale)].mask;
    const int root = s.settings.root;
    // the scale counts from the note played through MIDI Thru, otherwise from the root
    const int ref = midiThru && thruLastNote >= 0 ? thruLastNote : root;
    const int loop = juce::jmax (1, s.settings.loop);
    auto loopIndex = [loop] (long long k) { return (int) (((k % loop) + loop) % loop); };
    VoiceData v = s.base;
    // a control's value from x (0..1); in the table's order: Transpose before Osc 2 Interval, which
    // lands on the scale from where Transpose puts oscillator 1
    auto apply = [&] (size_t i, double x)
    {
        const auto& p = params[i];
        int value = randomValue (p, getParam (s.base, p), s.amounts[i], x);
        if (mask != 0 && isPitchInterval (p))
        {
            const int from = std::strcmp (p.id, "osc2Semi") == 0 ? ref + semisOfValue (getParam (v, *findParam ("tuneSemi"))) : ref;
            value = valueOfSemis (nearestInterval (semisOfValue (value), from, mask, root, -7, 7));
        }
        setParam (v, p, value);
        randomLive[i] = true;
    };

    if (shape == kShapeSampleHold)
    {
        // a new value at each step (up), or the loop's (down); all sent at once, on the step
        if (newStep)
        {
            for (size_t i = 0; i < n; ++i)
                if (s.amounts[i] != 0)
                    apply (i, s.amounts[i] > 0 ? randomDice.nextDouble() : loopRandom (s.seeds[i], (int) i, loopIndex (step)));
            for (size_t i = 0; i < n; ++i)
                if (s.amounts[i] != 0)
                {
                    put (i, v);
                    randomSentCc[i] = -1;
                }
        }
        queueCc (msgs);
        return;
    }

    // Smooth and the waves move all the time: each control's CC goes out when its value changes,
    // the biggest moves first, within the budget
    const double phase = juce::jlimit (0.0, 1.0, randomBeats / stepBeats - (double) step);
    for (size_t i = 0; i < n; ++i)
    {
        const int amount = s.amounts[i];
        if (amount == 0)
            continue;
        double x;
        if (shape == kShapeSmooth)
        {
            double a, b;
            if (amount < 0)
            {
                a = loopRandom (s.seeds[i], (int) i, loopIndex (step));
                b = loopRandom (s.seeds[i], (int) i, loopIndex (step + 1));
            }
            else
            {
                if (smoothStep[i] != step || ! randomLive[i])
                {
                    const bool next = randomLive[i] && smoothStep[i] == step - 1;
                    smoothFrom[i] = next ? smoothTo[i] : randomDice.nextDouble();
                    smoothTo[i] = randomDice.nextDouble();
                    smoothStep[i] = step;
                }
                a = smoothFrom[i];
                b = smoothTo[i];
            }
            x = a + (b - a) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * phase));
        }
        else
        {
            x = waveValue (shape, phase);
            if (amount < 0)
                x = 1.0 - x;   // upside down
        }
        apply (i, x);
    }

    struct Move { size_t i; int value, size; };
    std::vector<Move> moves;
    for (size_t i = 0; i < n; ++i)
    {
        if (s.amounts[i] == 0)
            continue;
        const auto m = ccMessagesFor (params[i], v);
        if (m.size() != 1)
            continue;
        if (m[0].second != randomSentCc[i])
            moves.push_back ({ i, m[0].second, randomSentCc[i] < 0 ? 128 : std::abs (m[0].second - randomSentCc[i]) });
    }
    std::sort (moves.begin(), moves.end(), [] (const Move& a, const Move& b) { return a.size > b.size; });
    for (const auto& m : moves)
    {
        if (randomTokens < 1.0)
            break;
        msgs.emplace_back (params[m.i].cc, m.value);
        randomSentCc[m.i] = m.value;
        randomTokens -= 1.0;
    }
    queueCc (msgs);
}

void OscarProcessor::saveRandomState (juce::ValueTree& state) const
{
    juce::ValueTree r (idRandom);
    r.setProperty (idShape, randomSet.shape, nullptr);
    r.setProperty (idRate, randomSet.rate, nullptr);
    r.setProperty (idLoop, randomSet.loop, nullptr);
    r.setProperty (idBpm, randomSet.bpm, nullptr);
    r.setProperty (idScale, randomSet.scale, nullptr);
    r.setProperty (idRoot, randomSet.root, nullptr);
    const auto& params = allParams();
    for (size_t i = 0; i < params.size(); ++i)
        if (randomAmounts[i] != 0 || randomSeeds[i] != 0)   // the loops too: a saved session keeps them
        {
            juce::ValueTree p (idRandomParam);
            p.setProperty (idId, params[i].id, nullptr);
            p.setProperty (idAmount, randomAmounts[i], nullptr);
            p.setProperty (idSeed, (juce::int64) randomSeeds[i], nullptr);
            r.appendChild (p, nullptr);
        }
    state.appendChild (r, nullptr);
}

void OscarProcessor::loadRandomState (const juce::ValueTree& r)
{
    randomSet = {};
    randomAmounts.fill (0);
    randomSeeds.fill (0);
    if (r.isValid())
    {
        randomSet.shape = juce::jlimit (0, (int) modShapes().size() - 1, (int) r.getProperty (idShape, randomSet.shape));
        randomSet.rate = juce::jlimit (0, (int) randomRates().size() - 1, (int) r.getProperty (idRate, randomSet.rate));
        randomSet.loop = juce::jlimit (1, 256, (int) r.getProperty (idLoop, randomSet.loop));
        randomSet.bpm = juce::jlimit (20, 400, (int) r.getProperty (idBpm, randomSet.bpm));
        randomSet.scale = juce::jlimit (0, (int) scales().size() - 1, (int) r.getProperty (idScale, 0));
        randomSet.root = juce::jlimit (0, 11, (int) r.getProperty (idRoot, 0));
        for (const auto& p : r)
        {
            const auto* def = findParam (p.getProperty (idId).toString().toStdString());
            const int i = def != nullptr ? indexOfParam (*def) : -1;
            if (i < 0 || ! isRandomisable (*def))
                continue;
            randomAmounts[(size_t) i] = juce::jlimit (-kRandomMax, kRandomMax, (int) p.getProperty (idAmount, 0));
            randomSeeds[(size_t) i] = (std::uint32_t) (juce::int64) p.getProperty (idSeed, 0);
        }
    }
    publishRandom();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OscarProcessor();
}
