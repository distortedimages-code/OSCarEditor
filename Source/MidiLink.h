// Direct connection to the OSCar's MIDI ports (bypasses the DAW, which can't
// route SysEx out of a plugin). Open/close: message thread. Sending: any
// thread (output access is locked). Incoming data is handed back on the
// message thread through Listener.

#pragma once

#include <JuceHeader.h>

#include "core/OscarParams.h"

class MidiLink : private juce::MidiInputCallback,
                 private juce::AsyncUpdater
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void sysexReceived (const std::vector<std::uint8_t>& message) = 0;   // F0 .. F7
        virtual void programChangeReceived (int channel, int program) = 0;
        virtual void controllerReceived (int channel, int controller, int value) = 0;
        virtual void receiveProgress (int bytesSoFar) = 0;                            // -1 = finished
    };

    explicit MidiLink (Listener& l);
    ~MidiLink() override;

    // Device handling (message thread). Identifier may be empty (= closed). Returns an error text.
    juce::String openOutput (const juce::String& identifier);
    juce::String openInput  (const juce::String& identifier);
    void closeAll();

    juce::String getOutputId() const   { return outputId; }
    juce::String getInputId() const    { return inputId; }
    bool isOutputOpen() const;
    bool isInputOpen() const           { return input != nullptr; }

    // Sending. Refused (returns false) while a SysEx transfer is still on the wire.
    bool sendSysex (const std::vector<std::uint8_t>& message);  // full F0 .. F7
    bool sendProgramChange (int channel, int program);
    bool sendControllers (int channel, const std::vector<oscar::CcMessage>& messages);
    bool sendShortMessages (const std::vector<juce::MidiMessage>& messages);   // notes, pitch bend

    // When the OSCar last sent a note-on for this note (its own keyboard), in
    // Time::getMillisecondCounterHiRes() units; 0 = never. Any thread.
    double lastNoteOnFromOscar (int note) const
    {
        return (note >= 0 && note < 128) ? noteOnFromOscarMs[(size_t) note].load() : 0.0;
    }

    bool isBusy() const;
    double busyRemainingMs() const;

    // Test hooks: a virtual output that captures everything sent, and a way to
    // feed messages as if they came from the OSCar. Not used by the plugin itself.
    void setTestOutput (std::function<void (const juce::MidiMessage&)> sink);
    void injectForTest (const juce::MidiMessage& m)   { handleIncomingMidiMessage (nullptr, m); }

private:
    void send (const juce::MidiMessage& m);   // outLock held
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handlePartialSysexMessage (juce::MidiInput*, const juce::uint8* data, int numBytes, double) override;
    void handleAsyncUpdate() override;

    Listener& listener;

    juce::CriticalSection outLock;               // guards 'output' and 'busyUntilMs'
    std::unique_ptr<juce::MidiOutput> output;
    std::function<void (const juce::MidiMessage&)> testOutput;
    double busyUntilMs = 0;

    std::unique_ptr<juce::MidiInput> input;
    juce::String outputId, inputId;

    juce::CriticalSection inLock;
    std::vector<std::vector<std::uint8_t>> pendingSysex;
    std::vector<std::pair<int, int>> pendingPrograms;
    std::vector<std::array<int, 3>> pendingControllers;
    std::array<std::atomic<double>, 128> noteOnFromOscarMs {};
    std::atomic<int> partialBytes { 0 };
    std::atomic<bool> partialActive { false };
    bool progressShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLink)
};
