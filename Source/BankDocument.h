// The plugin's model: the bank being edited, what we believe the OSCar holds,
// names, undo/redo, clipboard and file formats. Message thread only.

#pragma once

#include <JuceHeader.h>

#include "core/OscarParams.h"
#include "core/OscarSysex.h"

class BankDocument : public juce::ChangeBroadcaster
{
public:
    BankDocument();

    //==============================================================================
    // Data
    const oscar::VoiceBank& getBank() const                 { return bank; }
    const oscar::VoiceData& getVoice (int slot) const       { return bank.voices[(size_t) juce::jlimit (0, oscar::kNumVoices - 1, slot)]; }
    juce::String getName (int slot) const                   { return names[(size_t) juce::jlimit (0, oscar::kNumVoices - 1, slot)]; }
    juce::String getDisplayName (int slot) const;           // "-1  GlassandoLead" / "-1  (unnamed)"

    const std::optional<oscar::WaveBank>& getWaves() const  { return waves; }
    const std::optional<std::vector<std::uint8_t>>& getSequences() const { return sequences; }

    //==============================================================================
    // What the OSCar holds, as far as we know (set by a received dump or a send).
    enum class Sync { Unknown, InSync, Edited };
    Sync getVoiceSync() const;
    bool isSlotEdited (int slot) const;          // differs from the OSCar's known content
    bool isOscarVoiceStateKnown() const          { return oscarVoices.has_value(); }
    const std::optional<oscar::VoiceBank>& getOscarBank() const { return oscarVoices; }
    std::optional<oscar::VoiceData> getOscarVoice (int slot) const
    {
        if (! oscarVoices || slot < 0 || slot >= oscar::kNumVoices) return std::nullopt;
        return oscarVoices->voices[(size_t) slot];
    }
    bool areWavesEdited() const;
    juce::Array<int> slotsDifferingFromOscar() const;

    // True when the editor's copy of 'slot' holds changes the OSCar doesn't have: compared
    // with the OSCar's known content, or, when that is unknown, with the bank as it was
    // when last in step (project reload, dump, send, or a sound reported by the OSCar).
    bool hasUnsentEdits (int slot) const;
    // That version: the voice as last saved (project opened, sent to the OSCar, dumped, reported).
    const oscar::VoiceData& getBaselineVoice (int slot) const { return baseline.voices[(size_t) juce::jlimit (0, oscar::kNumVoices - 1, slot)]; }

    // The OSCar reported what 'slot' holds (patch CC dump right after it loaded the voice).
    // Updates what we know of the OSCar; with 'adoptIntoEditor' the editor's copy follows
    // (one undo step), otherwise the editor keeps its own version.
    void oscarReportedVoice (int slot, const oscar::VoiceData& v, bool adoptIntoEditor);

    //==============================================================================
    // Editing (every call records undo)
    void setParam (int slot, const oscar::ParamDef& p, int value);
    void setHarmonic (int table, int harmonic, int value);
    void rename (int slot, const juce::String& newName);
    void copySlot (int slot);
    bool canPaste() const                        { return clipboard.has_value(); }
    void pasteSlot (int slot);
    void swapSlots (int a, int b);
    void revertSlot (int slot);                  // back to the OSCar's known content
    void initSlot (int slot);                    // a plain starting sound named INIT (oscar::initVoice)
    void setVoice (int slot, const oscar::VoiceData& v, const juce::String& undoCoalesceKey = {});
    // Changes made through this with the same key are one undo step, however long they take,
    // until another change or endUndoGroup() (an A/B morph with pauses in it).
    void setVoiceInGroup (int slot, const oscar::VoiceData& v, const juce::String& groupKey);
    void endUndoGroup()                                     { lastCoalesceKey = {}; }
    void loadFactoryVoices();

    bool canUndo() const                         { return ! undoStack.empty(); }
    bool canRedo() const                         { return ! redoStack.empty(); }
    void undo();
    void redo();

    //==============================================================================
    // Transfers
    // Returns a human-readable report (comparison with the previously known state).
    juce::String receiveDump (const oscar::DumpContents& d);
    void markVoicesSent();
    void markWavesSent();

    //==============================================================================
    // Files
    // Loads .syx or .oscbank into the editor (not into the OSCar). Error text on failure.
    juce::Result loadFile (const juce::File& f, juce::String& report);
    juce::Result saveBankFile (const juce::File& f) const;    // .oscbank (names + data)
    juce::Result exportSyx (const juce::File& f) const;       // voices (+ waveforms if present)
    juce::Result exportVoice (int slot, const juce::File& f) const;   // .oscvoice
    juce::Result importVoice (int slot, const juce::File& f);

    //==============================================================================
    // Host state
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

    static juce::String bytesToHex (const std::uint8_t* d, size_t n);
    static bool hexToBytes (const juce::String& s, std::vector<std::uint8_t>& out);

private:
    struct Snapshot
    {
        oscar::VoiceBank bank;
        std::array<juce::String, oscar::kNumVoices> names;
        std::optional<oscar::WaveBank> waves;
    };

    Snapshot capture() const;
    void restore (const Snapshot&);
    void pushUndo (const juce::String& coalesceKey = {}, bool withoutTimeLimit = false);
    void changed()                               { sendChangeMessage(); }
    juce::String nameForReceivedVoice (const oscar::VoiceData& v, const Snapshot& before) const;

    oscar::VoiceBank bank;
    std::array<juce::String, oscar::kNumVoices> names;
    std::optional<oscar::WaveBank> waves;
    std::optional<std::vector<std::uint8_t>> sequences;

    std::optional<oscar::VoiceBank> oscarVoices;
    oscar::VoiceBank baseline;   // see hasUnsentEdits
    std::optional<oscar::WaveBank>  oscarWaves;

    std::optional<std::pair<oscar::VoiceData, juce::String>> clipboard;

    std::vector<Snapshot> undoStack, redoStack;
    juce::String lastCoalesceKey;
    double lastCoalesceTime = 0;
    static constexpr size_t maxUndo = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BankDocument)
};
