// Voice programme parameters for the OSCar (factory MIDI and Tauntek firmware).
//
// Everything here was cross-checked against the Tauntek firmware image
// (tntkoscr.bin, checksum 9D73) and the Tauntek CC chart:
//  - byte order: the firmware's CC handlers write the edit buffer in exactly
//    the order of the 64-nibble SysEx voice (26 knobs, then 12 switch nibbles);
//  - switch value sets: taken from the firmware's own CC lookup tables
//    (e.g. Glide/Filter Type/Function use 0,1,2,4,5,6 - bit 2 is a flag);
//  - Tauntek settings live in bit 3 of the Function, LFO Wave, Filter Type
//    and Trigger nibbles (firmware routines at 0x4800 / 0x481E).
// Switch *names* (which waveform is "Triangle" etc.) come from community
// documentation and are flagged unverified; values are never guessed. Glide Type,
// Function, Filter Type, Trigger, LFO Wave and Osc 2 Octave are checked against the
// OSCar's panel: its positions run in the same order, and the second half of Glide
// (FIX), Function (HOLD) and Filter Type (NO TRACK) is exactly the flag bit. The panel
// prints "N" for the first waveform position (triangle in contemporary reviews);
// Osc 2 Octave goes from -2 to +3 (panel and review), the factory voices use -1..+1.
// The oscillator waveforms stay flagged: the panel confirms positions 0-5 only, not
// the numbered waveforms -3..-12.

#pragma once

#include "OscarSysex.h"

#include <string>
#include <utility>
#include <vector>

namespace oscar
{
enum class ParamKind
{
    Knob,    // 8-bit value spread over two nibble bytes (low nibble first)
    Switch,  // bit field inside one nibble byte
    PwmRate  // Tauntek PWM rate: bit 3 of the Filter Type nibble + bit 3 of the Trigger nibble
};

// How the Tauntek firmware receives the parameter over CC.
enum class CcCoding
{
    None,
    Knob,       // RAM = max (1, 2 * cc)
    Steps16,    // RAM = cc / 8            (waveforms, arpeggiator nibble)
    Semitone,   // RAM = 14 - cc / 8       (cc 112..127 -> 0)
    Six,        // 0..5, ranges 0-20, 21-41, 42-62, 63-83, 84-104, 105-127
    SixSparse,  // same ranges -> 0, 1, 2, 4, 5, 6
    OctaveSet,  // 2..6, ranges 0-24, 25-49, 50-74, 75-99, 100-127
    Two,        // 0-63 -> 0, 64-127 -> 1
    PwmRate,    // 0..3, ranges 0-31, 32-63, 64-95, 96-127
    ArpNibble   // CC 51: whole arpeggiator nibble (Steps16); also copies its bit 3 into Function bit 2
};

struct Choice
{
    int value;
    std::string label;
};

struct ParamDef
{
    const char* id;          // stable identifier (files, UI, host parameters)
    const char* name;        // display name, OSCar panel wording
    const char* section;     // panel section
    ParamKind   kind;
    int         offset;      // wire byte inside the 64-byte voice
    std::uint8_t mask;       // Switch: bits owned inside the byte
    int         shift;       // Switch
    std::vector<Choice> choices;   // Switch / PwmRate: valid values, in display order
    bool        labelsVerified;
    int         cc;          // Tauntek CC number, -1 if none
    CcCoding    coding;
};

// Knob values the OSCar writes go from 0 to 254 (no 255 in the factory bank;
// over CC the firmware writes 1..254), so the editor never writes more.
constexpr int kKnobMaxEditable = 254;

const std::vector<ParamDef>& allParams();
const ParamDef* findParam (const std::string& id);

int  getParam (const VoiceData& v, const ParamDef& p);
// Returns false (and leaves 'v' untouched) if the value can't be stored.
bool setParam (VoiceData& v, const ParamDef& p, int value);

// "Init": a plain starting sound. Oscillator 1 alone on sawtooth (Osc Balance fully on 1, as printed
// on the panel; oscillator 2 waits on the same wave, barely detuned, so turning Balance brings it in
// without a change of tone), low-pass wide open without resonance, filter envelope amount at zero,
// organ-like amp envelope (instant attack, full sustain, short release), no LFO, no glide,
// Function Normal, Trigger Multi. The switches the factory voices agree on (osc 2 octave,
// keyboard octave, arpeggiator, Tauntek bits) keep that common value. Knob values are even or 1,
// so Live CC gives the OSCar exactly this voice.
VoiceData initVoice();

int  maxParamValue (const ParamDef& p);              // largest storable raw value
bool isValidChoice (const ParamDef& p, int value);   // value is one of p.choices
std::string choiceLabel (const ParamDef& p, int value); // label, or "Value n"

//==============================================================================
// Tauntek CC
using CcMessage = std::pair<int, int>;   // controller number, value 0..127

// The CC message(s) that put the OSCar's current sound to this parameter's value in 'v'.
// Empty if the parameter has no CC or the stored value can't be expressed over CC.
// Arpeggiator parameters return CC 51 followed by CC 50 (Function), because the
// firmware's CC 51 handler also overwrites Function bit 2.
std::vector<CcMessage> ccMessagesFor (const ParamDef& p, const VoiceData& v);

// What the firmware stores when it receives 'ccValue' (exact copy of its lookup tables).
int firmwareValueFromCc (CcCoding coding, int ccValue);

// CC value to send for a raw parameter value, -1 if not expressible.
int ccValueFor (CcCoding coding, int value);

// Compares a CC patch dump (cc -> value, as sent by the OSCar after a voice change)
// with a voice. Returns the names of parameters that differ; ignores parameters
// missing from the dump.
std::vector<std::string> compareWithCcDump (const VoiceData& v, const std::vector<int>& ccValues /* 128 entries, -1 = absent */);

// Rebuilds the sound the OSCar is playing from its Tauntek patch CC dump ("total recall").
// The dump is 7-bit, so the reference voice supplies what it can't carry: a knob keeps the
// reference value when that value is consistent with the dump (otherwise the candidate
// nearest to it is used), and parameters absent from the dump, parameters without a CC and
// the bits the editor doesn't own are copied from the reference. Switches come out exact.
struct CcDumpRecall
{
    VoiceData voice {};
    std::vector<std::string> changed;     // parameters that differ from the reference
    std::vector<std::string> unreadable;  // dump values that match no valid setting (left as in the reference)
};
CcDumpRecall voiceFromCcDump (const VoiceData& reference, const std::vector<int>& ccValues);

// What the firmware's patch dump (routine at 0x59F2) sends for a stored value.
int dumpCcFor (CcCoding coding, int value);

// The (wire byte, bit mask) pairs a parameter owns inside a voice.
std::vector<std::pair<int, std::uint8_t>> ownedBits (const ParamDef& p);

constexpr int kTempoCc = 54; // global, not stored in voices

} // namespace oscar
