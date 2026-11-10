#include "MidiLink.h"

#include "core/OscarSysex.h"
#include "ui/Lang.h"

MidiLink::MidiLink (Listener& l) : listener (l) {}

MidiLink::~MidiLink()
{
    closeAll();
    cancelPendingUpdate();
}

void MidiLink::closeAll()
{
    if (input != nullptr)
        input->stop();
    input.reset();
    inputId = {};

    const juce::ScopedLock sl (outLock);
    output.reset();
    outputId = {};
}

bool MidiLink::isOutputOpen() const
{
    const juce::ScopedLock sl (outLock);
    return output != nullptr || testOutput != nullptr;
}

void MidiLink::setTestOutput (std::function<void (const juce::MidiMessage&)> sink)
{
    const juce::ScopedLock sl (outLock);
    testOutput = std::move (sink);
    busyUntilMs = 0;
}

void MidiLink::send (const juce::MidiMessage& m)
{
    if (testOutput != nullptr)
        testOutput (m);
    else if (output != nullptr)
        output->sendMessageNow (m);
}

juce::String MidiLink::openOutput (const juce::String& identifier)
{
    std::unique_ptr<juce::MidiOutput> newOutput;
    if (identifier.isNotEmpty())
        newOutput = juce::MidiOutput::openDevice (identifier);

    {
        const juce::ScopedLock sl (outLock);
        output = std::move (newOutput);
        busyUntilMs = 0;
        outputId = output != nullptr ? identifier : juce::String();
    }

    if (identifier.isNotEmpty() && outputId.isEmpty())
        return lang::tr ("Could not open the MIDI output (unplugged, or used exclusively by another app).");
    return {};
}

juce::String MidiLink::openInput (const juce::String& identifier)
{
    if (input != nullptr)
        input->stop();
    input.reset();
    inputId = {};
    if (identifier.isEmpty())
        return {};

    input = juce::MidiInput::openDevice (identifier, this);
    if (input == nullptr)
        return lang::tr ("Could not open the MIDI input (unplugged, or used exclusively by another app).");

    inputId = identifier;
    input->start();
    return {};
}

//==============================================================================
double MidiLink::busyRemainingMs() const
{
    const juce::ScopedLock sl (outLock);
    return juce::jmax (0.0, busyUntilMs - juce::Time::getMillisecondCounterHiRes());
}

bool MidiLink::isBusy() const
{
    return busyRemainingMs() > 0.0;
}

bool MidiLink::sendSysex (const std::vector<std::uint8_t>& m)
{
    if (m.size() < 3 || m.front() != 0xF0 || m.back() != 0xF7)
        return false;

    const juce::ScopedLock sl (outLock);
    if ((output == nullptr && testOutput == nullptr) || busyUntilMs > juce::Time::getMillisecondCounterHiRes())
        return false;

    send (juce::MidiMessage::createSysExMessage (m.data() + 1, (int) m.size() - 2));

    // The interface plays the bytes out at 31250 baud; nothing else may be
    // sent before they have reached the OSCar (+10 % and a safety margin).
    busyUntilMs = juce::Time::getMillisecondCounterHiRes()
                + oscar::transferSeconds (m.size()) * 1000.0 * 1.1 + 250.0;
    return true;
}

bool MidiLink::sendProgramChange (int channel, int program)
{
    if (channel < 1 || channel > 16 || program < 0 || program > 127)
        return false;

    const juce::ScopedLock sl (outLock);
    if ((output == nullptr && testOutput == nullptr) || busyUntilMs > juce::Time::getMillisecondCounterHiRes())
        return false;

    send (juce::MidiMessage::programChange (channel, program));
    return true;
}

bool MidiLink::sendControllers (int channel, const std::vector<oscar::CcMessage>& messages)
{
    if (channel < 1 || channel > 16)
        return false;

    const juce::ScopedLock sl (outLock);
    if ((output == nullptr && testOutput == nullptr) || busyUntilMs > juce::Time::getMillisecondCounterHiRes())
        return false;

    for (const auto& [cc, value] : messages)
        if (cc >= 0 && cc <= 127 && value >= 0 && value <= 127)
            send (juce::MidiMessage::controllerEvent (channel, cc, value));
    return true;
}

bool MidiLink::sendShortMessages (const std::vector<juce::MidiMessage>& messages)
{
    const juce::ScopedLock sl (outLock);
    if ((output == nullptr && testOutput == nullptr) || busyUntilMs > juce::Time::getMillisecondCounterHiRes())
        return false;

    for (const auto& m : messages)
        if (! m.isSysEx() && m.getRawDataSize() <= 3)
            send (m);
    return true;
}

//==============================================================================
// MIDI thread
void MidiLink::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isNoteOnOrOff() || m.isProgramChange() || m.isController())
        lastChannel = m.getChannel();

    if (m.isNoteOn())
    {
        noteOnFromOscarMs[(size_t) m.getNoteNumber()] = juce::Time::getMillisecondCounterHiRes();
        return;
    }

    if (m.isSysEx())
    {
        const auto* raw = m.getRawData();
        std::vector<std::uint8_t> bytes (raw, raw + m.getRawDataSize());
        {
            const juce::ScopedLock sl (inLock);
            pendingSysex.push_back (std::move (bytes));
        }
        partialActive = false;
        triggerAsyncUpdate();
    }
    else if (m.isProgramChange())
    {
        {
            const juce::ScopedLock sl (inLock);
            pendingPrograms.emplace_back (m.getChannel(), m.getProgramChangeNumber());
        }
        triggerAsyncUpdate();
    }
    else if (m.isController())
    {
        {
            const juce::ScopedLock sl (inLock);
            pendingControllers.push_back ({ m.getChannel(), m.getControllerNumber(), m.getControllerValue() });
        }
        triggerAsyncUpdate();
    }
}

void MidiLink::handlePartialSysexMessage (juce::MidiInput*, const juce::uint8*, int numBytes, double)
{
    partialBytes = numBytes;
    partialActive = true;
    triggerAsyncUpdate();
}

// Message thread
void MidiLink::handleAsyncUpdate()
{
    std::vector<std::vector<std::uint8_t>> sysex;
    std::vector<std::pair<int, int>> programs;
    std::vector<std::array<int, 3>> controllers;
    {
        const juce::ScopedLock sl (inLock);
        sysex.swap (pendingSysex);
        programs.swap (pendingPrograms);
        controllers.swap (pendingControllers);
    }

    if (partialActive)
    {
        listener.receiveProgress (partialBytes);
        progressShown = true;
    }

    for (const auto& s : sysex)
        listener.sysexReceived (s);

    if (! sysex.empty() && progressShown && ! partialActive)
    {
        listener.receiveProgress (-1);
        progressShown = false;
    }

    // Order matters: a programme change comes before the CC dump it triggers.
    for (const auto& [ch, prog] : programs)
        listener.programChangeReceived (ch, prog);

    for (const auto& c : controllers)
        listener.controllerReceived (c[0], c[1], c[2]);
}
