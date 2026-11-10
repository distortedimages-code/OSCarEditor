#pragma once

#include <JuceHeader.h>

#include "BankDocument.h"
#include "MidiLink.h"
#include "Updater.h"
#include "core/Generator.h"
#include "ui/Lang.h"

class OscarProcessor : public juce::AudioProcessor,
                       private MidiLink::Listener,
                       private juce::ChangeListener,
                       private juce::Timer,
                       private juce::HighResolutionTimer
{
public:
    OscarProcessor();
    ~OscarProcessor() override;

    //==============================================================================
    BankDocument& getDocument()          { return document; }
    MidiLink& getMidi()                  { return midi; }
    juce::AudioProcessorValueTreeState& getHostParameters() { return hostParams; }

    // Status shown in the UI (message thread).
    juce::ChangeBroadcaster statusBroadcaster;
    juce::StringArray getLog() const     { return log; }
    juce::String getLastMessage() const  { return log.isEmpty() ? juce::String() : log[log.size() - 1]; }
    int getReceiveProgress() const       { return receiveBytes; }   // bytes, or -1

    //==============================================================================
    // Settings
    int  getSelectedSlot() const         { return selectedSlot; }
    void setSelectedSlot (int slot);
    int  getChannel() const              { return channel; }
    void setChannel (int ch);
    bool getFollowOscar() const          { return followOscar; }
    void setFollowOscar (bool b)         { followOscar = b; statusBroadcaster.sendChangeMessage(); }
    bool getSelectAfterSend() const      { return selectAfterSend; }
    void setSelectAfterSend (bool b)     { selectAfterSend = b; statusBroadcaster.sendChangeMessage(); }
    bool getLiveCc() const               { return liveCc; }
    void setLiveCc (bool b);

    // Play the OSCar from the DAW track: notes and pitch bend received by the plugin
    // are forwarded to the OSCar on its channel. Off by default.
    bool getMidiThru() const             { return midiThru; }
    void setMidiThru (bool b);

    // MIDI clock to the OSCar from the DAW's tempo and transport (24 per quarter note,
    // Start / Continue / Stop). Off by default: the DAW may already send clock to the OSCar.
    bool getClockOut() const             { return clockOut; }
    void setClockOut (bool b);

    // Interface language: "fr" (default) or "en". Shared by every instance.
    void setLanguage (const juce::String& code);
    juce::String getLanguage() const;
    juce::ChangeBroadcaster languageChanged;

    // Does the OSCar send its Tauntek patch CC dump when it changes voice?
    enum class CcDumpState { Unknown, Active, Missing };
    CcDumpState getCcDumpState() const   { return ccDumpState; }

    juce::String setOutputDevice (const juce::String& identifier);
    juce::String setInputDevice (const juce::String& identifier);

    //==============================================================================
    // Actions (message thread). Return an error text, empty on success.
    void editParam (int slot, const oscar::ParamDef& p, int value);   // UI edits go through here
    void chooseVoice (int slot);          // list click: select, and in live mode play it on the OSCar
    juce::String sendVoices (bool selectSlotAfter);
    juce::String sendWaves();
    juce::String sendSequences();
    juce::String selectOnOscar (int slot);

    static juce::File getBackupFolder();
    static juce::File getBanksFolder();          // the user's saved banks (.oscbank, .syx)
    static juce::File getVoicesFolder();         // the user's saved voices (.oscvoice)
    static juce::File getUserFolder();           // Documents/OXIde (moved from Documents/OSCar Editor)

    //==============================================================================
    // Sound generator (message thread). It works on the selected voice, which the OSCar
    // plays straight away when Live CC is on; nothing is stored in the OSCar until "Send".
    enum class GenMode { Vary, New, Breed };
    struct GenSettings
    {
        GenMode mode = GenMode::New;
        oscar::Family family = oscar::Family::Bass;
        double amount = 0.35;          // vary: 0 subtle .. 1 wild
        oscar::GenLocks locks;
        int parentA = 0, parentB = 12; // breed
    };
    struct GenEntry { oscar::VoiceData voice; juce::String label; juce::Time when; };

    GenSettings& genSettings()                              { return gen; }
    void generate();
    void genStep (int delta);                               // through the history
    void genBackToOriginal();
    void genKeep();                                         // ends the series
    const std::vector<GenEntry>& getGenHistory() const      { return genHistory; }
    int getGenIndex() const                                 { return genIndex; }
    int getGenSlot() const                                  { return genSlot; }
    void addLog (const juce::String& text);

    //==============================================================================
    // A/B morph (message thread): the selected voice becomes a blend of voices A and B, following the
    // host parameter "morph" (the crossfader, or DAW automation). Knobs glide from A to B, switches flip
    // half way. The blend follows the fader at most one full sweep per 'smoothing' seconds, so a sudden
    // move becomes a glide. The OSCar only gets the CC that changed, at most 12 every 33 ms (the biggest
    // moves first, the rest on the next steps): about a third of what a MIDI cable carries, which leaves
    // room for notes, clock and the Tauntek board's own work. Nothing is sent during a SysEx transfer.
    // Undo brings the voice back in one step.
    struct MorphSettings { int slotA = 0, slotB = 12; float smoothing = 0.3f; };
    const MorphSettings& morphSettings() const      { return morph; }
    void setMorphSources (int slotA, int slotB);
    void setMorphSmoothing (float seconds)          { morph.smoothing = juce::jlimit (0.0f, 2.0f, seconds); }
    float getMorphPosition() const                  { return morphPos; }
    void morphKeep();                               // the blend stays; the next move starts again from A and B
    static oscar::VoiceData blend (const oscar::VoiceData& a, const oscar::VoiceData& b, float t);

    // Set when the user chose "don't ask again" for sending edits (this session only).
    bool sendConfirmedThisSession = false;

    // Editor state kept while the window is closed (not saved in projects).
    int editorPage = 0;
    float editorScale = 1.0f;

    Updater& getUpdater()                { return updater; }

    //==============================================================================
    void prepareToPlay (double sr, int) override        { if (sr > 0) sampleRate = sr; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    // MidiLink::Listener (message thread)
    void sysexReceived (const std::vector<std::uint8_t>& message) override;
    void programChangeReceived (int ch, int program) override;
    void controllerReceived (int ch, int controller, int value) override;
    void receiveProgress (int bytes) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;  // document changed
    void timerCallback() override;              // message thread: CC dump detection
    void hiResTimerCallback() override;         // CC engine thread: automation + outbox

    // The OSCar is now playing 'slot' (just selected by us or on the OSCar).
    void oscarSelected (int slot);
    void pushDifferencesToOscar();
    void queueCc (const std::vector<oscar::CcMessage>& messages);
    void evaluateCcDump();
    void pumpThru();                            // CC engine thread
    void applyToSelected (const oscar::VoiceData& v, const juce::String& undoKey);
    oscar::Corpus learningCorpus() const;

    juce::File writeBackup (const juce::String& what, const std::vector<std::uint8_t>& bytes);
    void applyState (const juce::ValueTree& state);
    void saveGlobalPorts();
    juce::PropertiesFile& globalSettings();

    static juce::AudioProcessorValueTreeState::ParameterLayout createHostLayout();

    juce::SharedResourcePointer<lang::Holder> languageHolder;   // first: translations outlive everything below
    BankDocument document;
    MidiLink midi { *this };
    juce::AudioProcessorValueTreeState hostParams;

    juce::StringArray log;
    int receiveBytes = -1;

    int selectedSlot = 0;
    std::atomic<int> channel { 1 };
    bool followOscar = true;
    bool selectAfterSend = true;
    std::atomic<bool> liveCc { true };

    // What the OSCar's current sound holds, as far as we know (message thread).
    int oscarSlot = -1;
    std::optional<oscar::VoiceData> oscarSound;
    int selectionToken = 0;

    // CC patch dump being received from the OSCar
    std::vector<int> dumpValues = std::vector<int> (128, -1);
    int dumpCount = 0;
    double lastDumpCcMs = 0;

    GenSettings gen;
    std::vector<GenEntry> genHistory;           // entry 0 = the voice before the series
    int genIndex = -1, genSlot = -1;

    CcDumpState ccDumpState = CcDumpState::Unknown;
    double awaitingDumpSince = 0;               // a programme change came from the OSCar
    int ccDumpMisses = 0;

    // MIDI thru and clock: audio thread -> CC engine thread, each event due at a given time
    struct OutEvent
    {
        std::array<std::uint8_t, 3> data {};
        double when = 0;   // Time::getMillisecondCounterHiRes()
    };
    std::atomic<bool> midiThru { false }, clockOut { false };
    std::atomic<double> sampleRate { 44100.0 };
    static constexpr int thruCapacity = 2048;
    juce::AbstractFifo thruFifo { thruCapacity };
    std::array<OutEvent, thruCapacity> thruData {};
    double nextClockTick = 0;          // audio thread only: next tick, in 1/24 quarter notes
    bool clockRunning = false;         // audio thread only
    double predictedBlockMs = 0;       // audio thread only: expected start of the next block
    std::array<bool, 128> thruActive {}, thruEchoed {}, thruPendingOff {};   // CC engine thread only
    bool thruBent = false;                                                  // CC engine thread only

    // Outbox shared by the message thread and the CC engine thread
    juce::CriticalSection outboxLock;
    std::vector<oscar::CcMessage> outbox;

    // Host automation (CC engine thread only, except the resync flag)
    struct HostParam
    {
        std::atomic<float>* raw = nullptr;
        const oscar::ParamDef* def = nullptr;   // nullptr = tempo
        int last = -1;
    };
    std::vector<HostParam> hostList;

    // A/B morph
    MorphSettings morph;
    struct MorphTicker : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } morphTicker;
    void morphTick();
    void startMorphSession();
    void applyMorph (const oscar::VoiceData& v);
    bool flushMorphCc();                    // true while some CC still wait for their turn
    static constexpr int morphCcPerTick = 12;   // at 30 Hz: about a third of a MIDI cable
    std::atomic<float>* morphParam = nullptr;
    float morphTarget = 0.0f, morphPos = 0.0f;
    bool morphSeen = false, morphMoving = false, morphSession = false, morphPending = false;
    int morphSlot = -1;
    oscar::VoiceData morphA {}, morphB {}, morphSent {};
    std::atomic<bool> resyncHost { true };
    std::atomic<int> hostSuspend { 0 };   // > 0 while host values are being restored

    std::unique_ptr<juce::PropertiesFile> settingsFile;
    Updater updater { [this]() -> juce::PropertiesFile& { return globalSettings(); } };

    JUCE_DECLARE_WEAK_REFERENCEABLE (OscarProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscarProcessor)
};
