// OSCar MIDI System Exclusive codec (factory-MIDI OSCar, M1/M2 firmware).
//
// Pure C++17, no JUCE dependency, so it can be unit-tested on its own.
//
// Wire format (per the OSCar MIDI documentation and the codemechanic
// "oscar-sysex-grammar" reverse-engineering, checked against the factory bank):
//
//   F0  2B  <type>  <data...>  F7
//
//   type 1 = programmable waveforms (harmonic tables)   240 data bytes
//   type 2 = voice programmes (36 voices x 64 bytes)    2304 data bytes
//   type 3 = sequences                                    variable length
//
// Every data byte on the wire carries one 4-bit nibble (0x00..0x0F).
// 8-bit values are sent as two bytes, low nibble first.
//
// Design rule for this codec: the raw wire bytes are the source of truth.
// Nothing is ever re-synthesised from decoded values, so bits whose meaning
// is unknown are always preserved exactly.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oscar
{
constexpr std::uint8_t kSysexStart     = 0xF0;
constexpr std::uint8_t kSysexEnd       = 0xF7;
constexpr std::uint8_t kManufacturerId = 0x2B;

enum class BlockType : std::uint8_t
{
    Waveforms = 1,
    Voices    = 2,
    Sequences = 3
};

constexpr int kNumVoices     = 36;
constexpr int kVoiceSize     = 64;                       // wire bytes per voice
constexpr int kVoiceBankSize = kNumVoices * kVoiceSize;  // 2304

constexpr int kNumWaveTables = 5;                        // user waveforms -8 .. -12
constexpr int kNumHarmonics  = 24;
constexpr int kWaveTableSize = kNumHarmonics * 2;        // 48 wire bytes
constexpr int kWaveBankSize  = kNumWaveTables * kWaveTableSize; // 240

using VoiceData = std::array<std::uint8_t, kVoiceSize>;

struct VoiceBank
{
    std::array<VoiceData, kNumVoices> voices {};
    bool operator== (const VoiceBank& o) const { return voices == o.voices; }
    bool operator!= (const VoiceBank& o) const { return ! (*this == o); }
};

struct WaveBank
{
    std::array<std::uint8_t, kWaveBankSize> raw {};
    bool operator== (const WaveBank& o) const { return raw == o.raw; }
    bool operator!= (const WaveBank& o) const { return ! (*this == o); }

    int  getHarmonic (int table, int harmonic) const;
    void setHarmonic (int table, int harmonic, int value);
};

struct DumpContents
{
    std::optional<VoiceBank>                 voices;
    std::optional<WaveBank>                  waves;
    std::optional<std::vector<std::uint8_t>> sequences;   // raw nibble bytes, kept opaque
    std::vector<std::string>                 warnings;
    std::vector<BlockType>                   suspectBlocks; // flagged by the CR/LF damage check

    bool isSuspect (BlockType t) const
    {
        for (auto b : suspectBlocks) if (b == t) return true;
        return false;
    }

    bool isEmpty() const { return ! voices && ! waves && ! sequences; }
};

//==============================================================================
// Parsing. Both functions are all-or-nothing: on error, 'out' is left untouched
// and 'error' explains what was wrong.

// One complete message, F0 ... F7.
bool parseMessage (const std::uint8_t* data, std::size_t size,
                   DumpContents& out, std::string& error);

// Any number of complete messages back to back (a .syx file).
// Fails if the same block type appears twice (ambiguous file).
bool parseStream (const std::uint8_t* data, std::size_t size,
                  DumpContents& out, std::string& error);

inline bool parseStream (const std::vector<std::uint8_t>& bytes, DumpContents& out, std::string& error)
{
    return parseStream (bytes.data(), bytes.size(), out, error);
}

//==============================================================================
// Building. Each block is sent as its own message, exactly like the factory file.
std::vector<std::uint8_t> buildVoiceMessage    (const VoiceBank&);
std::vector<std::uint8_t> buildWaveMessage     (const WaveBank&);
std::vector<std::uint8_t> buildSequenceMessage (const std::vector<std::uint8_t>& raw);

// Everything present in 'd', in the order waveforms, voices, sequences.
std::vector<std::uint8_t> buildFile (const DumpContents& d);

//==============================================================================
// Voice slot naming. Order on the wire and for MIDI programme change is
// -1 .. -12 then 1 .. 24 (OSCar MIDI manual, "voice order").
std::string slotLabel (int index);               // "-1" .. "-12", "1" .. "24"
int         slotIndexFromLabel (int oscarNumber); // -12..-1, 1..24 -> 0..35, else -1
int         slotOscarNumber (int index);          // 0..35 -> -1..-12, 1..24

// Transfer time over a 31250 baud DIN link (10 bits per byte).
inline double transferSeconds (std::size_t numBytes) { return (double) numBytes * 10.0 / 31250.0; }

} // namespace oscar
