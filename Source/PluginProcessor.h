#pragma once

#include <JuceHeader.h>

#include "BankDocument.h"
#include "MidiLink.h"
#include "Updater.h"
#include "core/Generator.h"
#include "core/Performance.h"
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
    // The channel the OSCar sends on (0 until it sends something), and whether it differs from ours:
    // then, unless it is in omni mode, the OSCar doesn't hear the plugin (SPACE + a key from 1 to 16
    // on the OSCar sets it, easily by mistake).
    int  getOscarChannel() const         { return oscarChannel; }
    bool channelMismatch() const         { return oscarChannel > 0 && oscarChannel != channel; }
    void setChannel (int ch);
    // Recall the sound when a project (or a saved state) opens: the OSCar is put back exactly as the
    // plugin has it, whatever its memories hold: every value of the selected voice as CC (no programme
    // change, so the OSCar's memory is neither loaded nor compared), the global tempo, and the user
    // waveforms when the voice uses one (SysEx: the only way for them). No voice memory of the OSCar
    // is written. Tried for 10 s while the port opens. It replaces whatever the OSCar plays, a sound
    // made on its panel and not stored included: hence Ask, the default, which waits for the user's
    // answer (in the plugin's window, as soon as it is open). A preference of this computer.
    enum class RecallMode { Always = 0, Ask = 1, Never = 2 };
    RecallMode getRecallMode() const;
    void setRecallMode (RecallMode);
    bool isRecallWaiting() const         { return recallWaiting; }   // Ask: not answered yet
    void answerRecall (bool recall);     // Ask: recall it, or keep the OSCar's sound
    void recallNow();                    // the same, at once

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
    // A/B morph (message thread). Off by default: then the crossfader and its automation do nothing.
    // On (MORPH), the OSCar plays a blend of voices A and B that follows the host parameter "morph"
    // (the crossfader, or DAW automation): knobs glide from A to B, switches flip half way. It only goes
    // to the OSCar, as CC: no voice changes until Keep, which writes the blend into the selected voice
    // (one undo step). A and B follow their voices as they are edited. The blend follows the fader at
    // most one full sweep per 'smoothing' seconds, so a sudden move becomes a glide; turning MORPH on
    // or loading a voice at one end glides too. The OSCar only gets the CC that changed, at most 12
    // every 33 ms (the biggest moves first): about a third of what a MIDI cable carries. Nothing is
    // sent during a SysEx transfer. Turning MORPH off, or choosing another voice, gives the OSCar the
    // selected voice back; edits of the selected voice are not sent while MORPH is on (the blend plays).
    struct MorphSettings { int slotA = 0, slotB = 12; float smoothing = 0.3f; };
    const MorphSettings& morphSettings() const      { return morph; }
    void setMorphSources (int slotA, int slotB);
    void loadMorphSource (bool toB, int slot);      // voice 'slot' at end A (toB false) or B
    void setMorphSmoothing (float seconds)          { morph.smoothing = juce::jlimit (0.0f, 2.0f, seconds); }
    bool getMorphOn() const                         { return morphOn; }
    void setMorphOn (bool on);
    float getMorphPosition() const                  { return morphPos; }
    oscar::VoiceData getMorphBlend() const;         // what MORPH plays (or would play, off)
    void morphKeep();                               // the blend into the selected voice
    static oscar::VoiceData blend (const oscar::VoiceData& a, const oscar::VoiceData& b, float t);

    //==============================================================================
    // Random and scale (message thread), as in MIFM1. Every knob and fader, Transpose, Osc 2 Interval
    // and Osc 2 Octave can get a random amount (-100..100, see core/Performance.h). At each step (on
    // the DAW's bars while it plays; otherwise at its tempo, or at 'bpm' without a DAW) the OSCar gets
    // the new values as CC, Live CC on; the voice itself never changes. Back to 0, the control's own
    // value is sent again (also when the randoms stop: all back to 0, Live CC off).
    // SHAPE: S&H (the values change at each step), Smooth (they glide from one to the next) or one of
    // MIFM1's LFO waves, which every control follows, a cycle per RATE; the waves and the glides are
    // sent as the values change, at most 400 CC a second (about a third of a MIDI cable).
    // With a scale, Transpose and Osc 2 Interval only take its notes (counted from the root; the
    // randoms, from the last note played through MIDI Thru), and the notes MIDI Thru passes on move to
    // the nearest note of the scale. Saved with the session.
    struct RandomSettings
    {
        int shape = oscar::kShapeSampleHold;        // index in oscar::modShapes()
        bool sync = true;                           // on the tempo (rate) or free (freeMs)
        int rate = oscar::defaultRandomRate();     // index in oscar::randomRates()
        double freeMs = 250.0;
        int loop = oscar::kDefaultLoopLength;       // S&H and Smooth: 0, new values; 2..64, a loop of so many steps
        bool bipolar = true;                        // either side of the control's value, or one side
        int bpm = 120;
        int scale = 0, root = 0;                    // index in oscar::scales(), 0 = C
    };
    const RandomSettings& randomSettings() const    { return randomSet; }
    void setRandomShape (int index);
    void setRandomSync (bool);
    void setRandomFreeMs (double ms);
    void setRandomBipolar (bool);
    // what a random plays now on this control (its value, 0..254 or a switch position), -1: none
    int getRandomShown (const oscar::ParamDef&) const;
    void setRandomRate (int index);
    void setRandomLoop (int steps);
    void setRandomBpm (int bpm);
    void setScale (int index);
    void setScaleRoot (int root);
    int  getRandomAmount (const oscar::ParamDef&) const;
    void setRandomAmount (const oscar::ParamDef&, int amount);   // back to 0: that loop is drawn again next time
    bool hasRandoms() const;
    void clearRandoms();
    // Switched off without being lost: one control's modulation, or all of them (the OSCar gets
    // the voice's own values meanwhile).
    bool isRandomMuted (const oscar::ParamDef&) const;
    void setRandomMuted (const oscar::ParamDef&, bool);
    bool getRandomBypass() const                    { return randomBypass; }
    void setRandomBypass (bool b)                   { randomBypass = b; publishRandom(); }
    void newRandomLoops();                          // every looped random draws another loop
    int  getRandomSteps() const                     { return randomSteps.load(); }   // steps played so far
    bool isRandomPlaying() const                    { return randomPlaying.load(); }
    double getHostTempo() const                     { return hostTempoSeen.load(); } // 0 until a DAW gives one

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
    void pumpThru (std::uint16_t scaleMask, int scaleRoot);   // CC engine thread
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
    bool recallWaiting = false;
    int recallStage = 0;                        // 0: nothing to do; 1: waveforms (if used); 2: the voice
    double recallAt = 0, recallGiveUp = 0;
    void recallTick (double now);
    bool selectAfterSend = true;
    std::atomic<bool> liveCc { true };

    // What the OSCar's current sound holds, as far as we know (message thread).
    int oscarSlot = -1;
    int oscarChannel = 0;
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
    std::array<bool, 128> thruActive {}, thruEchoed {}, thruPendingOff {};   // CC engine thread only: by note received
    std::array<int, 128> thruMap {}, thruOutCount {};   // CC engine thread only: the note sent for each received (scale)
    int thruLastNote = -1;                                                  // CC engine thread only: the last sent
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
    void morphOff (const juce::String& why);   // the OSCar gets the voice back (paced)
    bool flushMorphCc (const oscar::VoiceData& target);   // true while some CC still wait for their turn
    static constexpr int morphCcPerTick = 12;   // at 30 Hz: about a third of a MIDI cable
    std::atomic<float>* morphParam = nullptr;
    float morphTarget = 0.0f, morphPos = 0.0f;
    bool morphSeen = false, morphMoving = false, morphOn = false, morphPending = false;
    int morphSlot = -1;                     // the voice the OSCar played when MORPH came on
    oscar::VoiceData morphHeard {}, morphSent {};   // the blend; what the OSCar got of it
    oscar::VoiceData morphFrom {};          // MORPH just on, or a voice just loaded: the sound glides from this one
    float morphFade = 1.0f;                 // 0..1 through that glide; 1 when there is none
    std::atomic<bool> resyncHost { true };
    std::atomic<int> hostSuspend { 0 };   // > 0 while host values are being restored

    // Random and scale
    static constexpr int kMaxParams = 64;
    RandomSettings randomSet;
    std::array<int, kMaxParams> randomAmounts {};
    std::array<std::uint32_t, kMaxParams> randomSeeds {};
    std::array<bool, kMaxParams> randomMuted {};
    bool randomBypass = false;
    void publishRandom();                       // message thread -> CC engine thread
    struct RandomShared
    {
        RandomSettings settings;
        std::array<int, kMaxParams> amounts {};
        std::array<std::uint32_t, kMaxParams> seeds {};
        oscar::VoiceData base {};               // the selected voice
        bool live = false;                      // Live CC
        bool plays = false;                     // the OSCar plays the selected voice (or nothing known)
    };
    juce::SpinLock randomLock;
    RandomShared randomShared;                  // under randomLock
    // the DAW's tempo and position at the start of the last block (audio thread -> CC engine thread)
    struct HostTime { double bpm = 0, ppq = 0, at = 0; bool playing = false; };
    juce::SpinLock hostTimeLock;
    HostTime hostTime;                          // under hostTimeLock
    std::atomic<double> hostTempoSeen { 0.0 };
    // CC engine thread only
    void randomTick (const RandomShared&, double now);
    int quantizedHostValue (const RandomShared&, const oscar::ParamDef&, int value) const;
    double randomBeats = 0, randomLastMs = 0, randomFreeMs = 0;
    std::array<std::atomic<int>, kMaxParams> randomShown {};
    long long randomLastStep = -1;
    std::array<bool, kMaxParams> randomLive {};   // sent with a random, not yet put back
    std::array<int, kMaxParams> randomSentCc {};  // the waves and glides: the last CC value sent (-1: none)
    std::array<double, kMaxParams> smoothFrom {}, smoothTo {};   // Smooth, new values: the glide of this step
    std::array<long long, kMaxParams> smoothStep {};
    double randomTokens = 12.0;                   // CC the waves may send now (400 a second, 12 at once)
    juce::Random randomDice;
    std::atomic<int> randomSteps { 0 };
    std::atomic<bool> randomPlaying { false };
    void saveRandomState (juce::ValueTree&) const;
    void loadRandomState (const juce::ValueTree&);

    std::unique_ptr<juce::PropertiesFile> settingsFile;
    Updater updater { [this]() -> juce::PropertiesFile& { return globalSettings(); } };

    JUCE_DECLARE_WEAK_REFERENCEABLE (OscarProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscarProcessor)
};
