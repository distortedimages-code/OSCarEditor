#include "BankDocument.h"

#include "core/FactoryBank.h"
#include "ui/Lang.h"

using lang::tr;

using namespace oscar;

namespace
{
    VoiceBank makeFactoryBank()
    {
        VoiceBank b;
        for (int i = 0; i < kNumVoices; ++i)
            for (int k = 0; k < kVoiceSize; ++k)
                b.voices[(size_t) i][(size_t) k] = factory::kVoiceBank[i * kVoiceSize + k];
        return b;
    }

    juce::String listSlots (juce::Array<int> slots)
    {
        std::sort (slots.begin(), slots.end(), [] (int a, int b) { return oscar::positionOfSlot (a) < oscar::positionOfSlot (b); });
        juce::StringArray s;
        for (auto i : slots)
            s.add (slotLabel (i));
        return s.joinIntoString (", ");
    }

    const juce::Identifier idBank ("OSCarBank"), idVoices ("voices"), idWaves ("waves"),
                           idSeq ("sequences"), idNames ("names"), idVersion ("version"),
                           idVoice ("OSCarVoice"), idName ("name"), idData ("data");

    constexpr int fileFormatVersion = 1;
}

BankDocument::BankDocument()
{
    bank = makeFactoryBank();
    baseline = bank;
    for (int i = 0; i < kNumVoices; ++i)
        names[(size_t) i] = factory::kVoiceNames[i];
}

juce::String BankDocument::getDisplayName (int slot) const
{
    const auto n = getName (slot);
    return n.isEmpty() ? juce::String ("(unnamed)") : n;
}

//==============================================================================
BankDocument::Sync BankDocument::getVoiceSync() const
{
    if (! oscarVoices)
        return Sync::Unknown;
    return *oscarVoices == bank ? Sync::InSync : Sync::Edited;
}

bool BankDocument::isSlotEdited (int slot) const
{
    if (! oscarVoices || slot < 0 || slot >= kNumVoices)
        return false;
    return oscarVoices->voices[(size_t) slot] != bank.voices[(size_t) slot];
}

bool BankDocument::areWavesEdited() const
{
    return waves.has_value() && (! oscarWaves || *oscarWaves != *waves);
}

juce::Array<int> BankDocument::slotsDifferingFromOscar() const
{
    juce::Array<int> r;
    for (int i = 0; i < kNumVoices; ++i)
        if (isSlotEdited (i))
            r.add (i);
    return r;
}

//==============================================================================
BankDocument::Snapshot BankDocument::capture() const
{
    return { bank, names, waves };
}

void BankDocument::restore (const Snapshot& s)
{
    bank = s.bank;
    names = s.names;
    waves = s.waves;
}

void BankDocument::pushUndo (const juce::String& coalesceKey, bool withoutTimeLimit)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();

    // Consecutive changes of the same control (a knob drag) become one undo step.
    if (coalesceKey.isNotEmpty() && coalesceKey == lastCoalesceKey
        && (withoutTimeLimit || now - lastCoalesceTime < 1500.0) && ! undoStack.empty())
    {
        lastCoalesceTime = now;
        return;
    }

    undoStack.push_back (capture());
    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    lastCoalesceKey = coalesceKey;
    lastCoalesceTime = now;
}

void BankDocument::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (capture());
    restore (undoStack.back());
    undoStack.pop_back();
    lastCoalesceKey = {};
    changed();
}

void BankDocument::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (capture());
    restore (redoStack.back());
    redoStack.pop_back();
    lastCoalesceKey = {};
    changed();
}

//==============================================================================
void BankDocument::setParam (int slot, const ParamDef& p, int value)
{
    if (slot < 0 || slot >= kNumVoices)
        return;

    auto v = bank.voices[(size_t) slot];
    if (! oscar::setParam (v, p, value) || v == bank.voices[(size_t) slot])
        return;

    pushUndo ("p:" + juce::String (slot) + ":" + p.id);
    bank.voices[(size_t) slot] = v;
    changed();
}

void BankDocument::setHarmonic (int table, int harmonic, int value)
{
    if (! waves) return;
    auto w = *waves;
    w.setHarmonic (table, harmonic, value);
    if (w == *waves) return;
    pushUndo ("h:" + juce::String (table));
    waves = w;
    changed();
}

void BankDocument::rename (int slot, const juce::String& newName)
{
    if (slot < 0 || slot >= kNumVoices) return;
    const auto n = newName.trim().substring (0, 24);
    if (n == names[(size_t) slot]) return;
    pushUndo();
    names[(size_t) slot] = n;
    changed();
}

void BankDocument::copySlot (int slot)
{
    if (slot < 0 || slot >= kNumVoices) return;
    clipboard = std::make_pair (bank.voices[(size_t) slot], names[(size_t) slot]);
    changed();
}

void BankDocument::pasteSlot (int slot)
{
    if (! clipboard || slot < 0 || slot >= kNumVoices) return;
    pushUndo();
    bank.voices[(size_t) slot] = clipboard->first;
    names[(size_t) slot] = clipboard->second;
    changed();
}

void BankDocument::initSlot (int slot)
{
    if (slot < 0 || slot >= kNumVoices) return;
    pushUndo();
    bank.voices[(size_t) slot] = oscar::initVoice();
    names[(size_t) slot] = "INIT";
    changed();
}

void BankDocument::swapSlots (int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= kNumVoices || b >= kNumVoices) return;
    pushUndo();
    std::swap (bank.voices[(size_t) a], bank.voices[(size_t) b]);
    std::swap (names[(size_t) a], names[(size_t) b]);
    changed();
}

void BankDocument::revertSlot (int slot)
{
    if (! isSlotEdited (slot)) return;
    pushUndo();
    bank.voices[(size_t) slot] = oscarVoices->voices[(size_t) slot];
    changed();
}

void BankDocument::setVoice (int slot, const VoiceData& v, const juce::String& undoCoalesceKey)
{
    if (slot < 0 || slot >= kNumVoices || bank.voices[(size_t) slot] == v)
        return;
    for (auto n : v)
        if (n > 0x0F)
            return;   // not a valid voice
    pushUndo (undoCoalesceKey);
    bank.voices[(size_t) slot] = v;
    changed();
}

void BankDocument::setVoiceInGroup (int slot, const VoiceData& v, const juce::String& groupKey)
{
    if (slot < 0 || slot >= kNumVoices || bank.voices[(size_t) slot] == v)
        return;
    for (auto n : v)
        if (n > 0x0F)
            return;   // not a valid voice
    pushUndo (groupKey, true);
    bank.voices[(size_t) slot] = v;
    changed();
}

void BankDocument::loadFactoryVoices()
{
    pushUndo();
    bank = makeFactoryBank();
    for (int i = 0; i < kNumVoices; ++i)
        names[(size_t) i] = factory::kVoiceNames[i];
    changed();
}

//==============================================================================
juce::String BankDocument::nameForReceivedVoice (const VoiceData& v, const Snapshot& before) const
{
    // Same slot unchanged -> keep its name; voice moved -> follow it; factory voice -> factory name.
    for (int i = 0; i < kNumVoices; ++i)
        if (before.bank.voices[(size_t) i] == v && before.names[(size_t) i].isNotEmpty())
            return before.names[(size_t) i];

    const auto fb = makeFactoryBank();
    for (int i = 0; i < kNumVoices; ++i)
        if (fb.voices[(size_t) i] == v)
            return factory::kVoiceNames[i];

    return {};
}

juce::String BankDocument::receiveDump (const DumpContents& d)
{
    juce::StringArray report;
    pushUndo();
    const auto before = capture();

    if (d.voices && ! d.isSuspect (BlockType::Voices))
    {
        if (oscarVoices)
        {
            juce::Array<int> differ;
            for (int i = 0; i < kNumVoices; ++i)
                if (oscarVoices->voices[(size_t) i] != d.voices->voices[(size_t) i])
                    differ.add (i);

            if (differ.isEmpty())
                report.add (tr ("Voices received: identical to the last known OSCar contents (OK)."));
            else
                report.add (tr ("Voices received: %1 voice(s) differ from the last known OSCar contents: %2", { juce::String (differ.size()), listSlots (differ) }));
        }
        else
        {
            report.add (tr ("Voices received (36)."));
        }

        const auto unsent = slotsDifferingFromOscar();
        if (! unsent.isEmpty())
            report.add (tr ("Unsent edits in %1 were replaced by the dump (Undo brings them back).", { listSlots (unsent) }));

        // Pending edits only matter when compared to the old OSCar state; recompute names after.
        for (int i = 0; i < kNumVoices; ++i)
        {
            const auto& v = d.voices->voices[(size_t) i];
            if (v != before.bank.voices[(size_t) i] || before.names[(size_t) i].isEmpty())
                names[(size_t) i] = nameForReceivedVoice (v, before);
        }

        bank = *d.voices;
        oscarVoices = bank;
        baseline = bank;
    }

    if (d.waves && ! d.isSuspect (BlockType::Waveforms))
    {
        if (oscarWaves && *oscarWaves == *d.waves)
            report.add (tr ("Waveforms received: identical to the last known OSCar contents (OK)."));
        else
            report.add (tr ("Waveforms received."));
        waves = d.waves;
        oscarWaves = waves;
    }

    if (d.sequences && ! d.isSuspect (BlockType::Sequences))
    {
        sequences = d.sequences;
        report.add (tr ("Sequences received (%1 bytes, kept as a backup).", { juce::String ((int) d.sequences->size()) }));
    }

    for (const auto& w : d.warnings)
        report.add (tr (juce::String (w)));

    changed();
    return report.joinIntoString ("\n");
}

void BankDocument::markVoicesSent()
{
    oscarVoices = bank;
    baseline = bank;
    changed();
}

bool BankDocument::hasUnsentEdits (int slot) const
{
    if (slot < 0 || slot >= kNumVoices)
        return false;
    const auto& reference = oscarVoices ? oscarVoices->voices[(size_t) slot] : baseline.voices[(size_t) slot];
    return bank.voices[(size_t) slot] != reference;
}

void BankDocument::oscarReportedVoice (int slot, const VoiceData& v, bool adoptIntoEditor)
{
    if (slot < 0 || slot >= kNumVoices)
        return;

    if (oscarVoices)
        oscarVoices->voices[(size_t) slot] = v;
    baseline.voices[(size_t) slot] = v;

    if (adoptIntoEditor && bank.voices[(size_t) slot] != v)
    {
        pushUndo();
        bank.voices[(size_t) slot] = v;
    }
    changed();
}

void BankDocument::markWavesSent()
{
    if (waves)
        oscarWaves = waves;
    changed();
}

//==============================================================================
juce::String BankDocument::bytesToHex (const std::uint8_t* d, size_t n)
{
    static const char* digits = "0123456789ABCDEF";
    juce::String s;
    s.preallocateBytes (n * 2 + 1);
    for (size_t i = 0; i < n; ++i)
    {
        s += juce::String::charToString ((juce::juce_wchar) digits[(d[i] >> 4) & 0x0F]);
        s += juce::String::charToString ((juce::juce_wchar) digits[d[i] & 0x0F]);
    }
    return s;
}

bool BankDocument::hexToBytes (const juce::String& s, std::vector<std::uint8_t>& out)
{
    const auto t = s.removeCharacters (" \r\n\t");
    if (t.length() % 2 != 0)
        return false;

    std::vector<std::uint8_t> r;
    r.reserve ((size_t) t.length() / 2);
    for (int i = 0; i < t.length(); i += 2)
    {
        const int hi = juce::CharacterFunctions::getHexDigitValue (t[i]);
        const int lo = juce::CharacterFunctions::getHexDigitValue (t[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        r.push_back ((std::uint8_t) ((hi << 4) | lo));
    }
    out = std::move (r);
    return true;
}

juce::ValueTree BankDocument::toValueTree() const
{
    juce::ValueTree v (idBank);
    v.setProperty (idVersion, fileFormatVersion, nullptr);

    std::vector<std::uint8_t> flat;
    for (const auto& voice : bank.voices)
        flat.insert (flat.end(), voice.begin(), voice.end());
    v.setProperty (idVoices, bytesToHex (flat.data(), flat.size()), nullptr);

    juce::StringArray n;
    for (const auto& s : names)
        n.add (s);
    v.setProperty (idNames, n.joinIntoString ("\n"), nullptr);

    if (waves)
        v.setProperty (idWaves, bytesToHex (waves->raw.data(), waves->raw.size()), nullptr);
    if (sequences)
        v.setProperty (idSeq, bytesToHex (sequences->data(), sequences->size()), nullptr);
    return v;
}

namespace
{
    // Validates a whole bank tree before anything is applied.
    struct ParsedBank
    {
        VoiceBank bank;
        std::array<juce::String, kNumVoices> names;
        std::optional<WaveBank> waves;
        std::optional<std::vector<std::uint8_t>> sequences;
    };

    bool allNibbles (const std::vector<std::uint8_t>& b)
    {
        for (auto x : b) if (x > 0x0F) return false;
        return true;
    }

    juce::Result parseBankTree (const juce::ValueTree& v, ParsedBank& out)
    {
        if (! v.hasType (idBank))
            return juce::Result::fail (tr ("Not an OSCar bank."));
        if ((int) v.getProperty (idVersion, 0) > fileFormatVersion)
            return juce::Result::fail (tr ("This bank was saved by a newer version of the plugin."));

        ParsedBank p;
        std::vector<std::uint8_t> bytes;
        if (! BankDocument::hexToBytes (v.getProperty (idVoices).toString(), bytes)
            || bytes.size() != (size_t) kVoiceBankSize || ! allNibbles (bytes))
            return juce::Result::fail (tr ("The voice data in the bank is damaged."));
        for (int i = 0; i < kNumVoices; ++i)
            std::copy (bytes.begin() + i * kVoiceSize, bytes.begin() + (i + 1) * kVoiceSize,
                       p.bank.voices[(size_t) i].begin());

        juce::StringArray n;
        n.addLines (v.getProperty (idNames).toString());
        for (int i = 0; i < kNumVoices; ++i)
            p.names[(size_t) i] = i < n.size() ? n[i].trim() : juce::String();

        if (v.hasProperty (idWaves))
        {
            if (! BankDocument::hexToBytes (v.getProperty (idWaves).toString(), bytes)
                || bytes.size() != (size_t) kWaveBankSize || ! allNibbles (bytes))
                return juce::Result::fail (tr ("The waveform data in the bank is damaged."));
            WaveBank w;
            std::copy (bytes.begin(), bytes.end(), w.raw.begin());
            p.waves = w;
        }

        if (v.hasProperty (idSeq))
        {
            if (! BankDocument::hexToBytes (v.getProperty (idSeq).toString(), bytes) || ! allNibbles (bytes))
                return juce::Result::fail (tr ("The sequence data in the bank is damaged."));
            p.sequences = bytes;
        }

        out = std::move (p);
        return juce::Result::ok();
    }
}

void BankDocument::fromValueTree (const juce::ValueTree& v)
{
    ParsedBank p;
    if (parseBankTree (v, p).failed())
        return;

    bank = p.bank;
    names = p.names;
    waves = p.waves;
    sequences = p.sequences;
    // A reloaded project says nothing reliable about what is in the OSCar now.
    oscarVoices.reset();
    baseline = bank;
    oscarWaves.reset();
    undoStack.clear();
    redoStack.clear();
    changed();
}

//==============================================================================
juce::Result BankDocument::loadFile (const juce::File& f, juce::String& report)
{
    juce::StringArray lines;

    if (f.hasFileExtension ("oscbank"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr)
            return juce::Result::fail (tr ("Could not read %1.", { f.getFileName() }));

        ParsedBank p;
        auto r = parseBankTree (juce::ValueTree::fromXml (*xml), p);
        if (r.failed())
            return r;

        pushUndo();
        bank = p.bank;
        names = p.names;
        if (p.waves)     waves = p.waves;
        if (p.sequences) sequences = p.sequences;
        lines.add (tr ("Loaded %1 into the editor (not yet sent to the OSCar).", { f.getFileName() }));
    }
    else
    {
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb))
            return juce::Result::fail (tr ("Could not read %1.", { f.getFileName() }));

        DumpContents d;
        std::string err;
        if (! parseStream (static_cast<const std::uint8_t*> (mb.getData()), mb.getSize(), d, err))
            return juce::Result::fail (f.getFileName() + ": " + tr (juce::String (err)));

        if (d.isEmpty())
            return juce::Result::fail (tr ("%1 contains no OSCar data.", { f.getFileName() }));

        pushUndo();
        const auto before = capture();
        juce::StringArray what;

        if (d.voices && ! d.isSuspect (BlockType::Voices))
        {
            for (int i = 0; i < kNumVoices; ++i)
                names[(size_t) i] = nameForReceivedVoice (d.voices->voices[(size_t) i], before);
            bank = *d.voices;
            what.add (tr ("voices"));
        }
        if (d.waves && ! d.isSuspect (BlockType::Waveforms))
        {
            waves = d.waves;
            what.add (tr ("waveforms"));
        }
        if (d.sequences && ! d.isSuspect (BlockType::Sequences))
        {
            sequences = d.sequences;
            what.add (tr ("sequences"));
        }

        if (what.isEmpty())
            lines.add (tr ("Nothing usable was loaded from %1.", { f.getFileName() }));
        else
            lines.add (tr ("Loaded %1 from %2 into the editor (not yet sent to the OSCar).", { what.joinIntoString (", "), f.getFileName() }));

        for (const auto& w : d.warnings)
            lines.add (tr (juce::String (w)));
    }

    report = lines.joinIntoString ("\n");
    changed();
    return juce::Result::ok();
}

juce::Result BankDocument::saveBankFile (const juce::File& f) const
{
    auto xml = toValueTree().createXml();
    if (xml == nullptr || ! xml->writeTo (f))
        return juce::Result::fail (tr ("Could not write %1.", { f.getFileName() }));
    return juce::Result::ok();
}

juce::Result BankDocument::exportSyx (const juce::File& f) const
{
    DumpContents d;
    d.voices = bank;
    d.waves = waves;
    const auto bytes = buildFile (d);
    if (! f.replaceWithData (bytes.data(), bytes.size()))
        return juce::Result::fail (tr ("Could not write %1.", { f.getFileName() }));
    return juce::Result::ok();
}

juce::Result BankDocument::exportVoice (int slot, const juce::File& f) const
{
    if (slot < 0 || slot >= kNumVoices)
        return juce::Result::fail (tr ("No voice selected."));
    juce::XmlElement x (idVoice.toString());
    x.setAttribute (idVersion, fileFormatVersion);
    x.setAttribute (idName, names[(size_t) slot]);
    const auto& v = bank.voices[(size_t) slot];
    x.setAttribute (idData, bytesToHex (v.data(), v.size()));
    if (! x.writeTo (f))
        return juce::Result::fail (tr ("Could not write %1.", { f.getFileName() }));
    return juce::Result::ok();
}

juce::Result BankDocument::importVoice (int slot, const juce::File& f)
{
    if (slot < 0 || slot >= kNumVoices)
        return juce::Result::fail (tr ("No voice selected."));

    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName (idVoice.toString()))
        return juce::Result::fail (tr ("%1 is not an OSCar voice file.", { f.getFileName() }));

    std::vector<std::uint8_t> bytes;
    if (! hexToBytes (x->getStringAttribute (idData), bytes) || bytes.size() != (size_t) kVoiceSize
        || ! allNibbles (bytes))
        return juce::Result::fail (tr ("The voice data in %1 is damaged.", { f.getFileName() }));

    pushUndo();
    std::copy (bytes.begin(), bytes.end(), bank.voices[(size_t) slot].begin());
    names[(size_t) slot] = x->getStringAttribute (idName).trim().substring (0, 24);
    changed();
    return juce::Result::ok();
}
