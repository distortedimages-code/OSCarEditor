#include "OscarSysex.h"

#include <algorithm>
#include <cstdio>

namespace oscar
{
namespace
{
    bool isNibble (std::uint8_t b) noexcept { return b <= 0x0F; }

    // Heuristic, for files only (MIDI cannot do this): a file that went through
    // a text-mode copy has every 0x0A turned into 0x0D 0x0A. So: no 0x0D and no
    // 0x0A on its own, many pairs, and the pairs at odd and even places alike
    // (a copy shifts the bytes). Real levels can make pairs too (173 is 0D 0A),
    // but always at the same place in a byte, never a mix: the damaged archive
    // has 27 pairs, 13 even and 14 odd.
    bool looksLikeCrLfDamage (const std::uint8_t* d, std::size_t n) noexcept
    {
        std::size_t even = 0, odd = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (d[i] == 0x0D)
            {
                if (i + 1 >= n || d[i + 1] != 0x0A)
                    return false;   // a CR alone
                ++((i % 2 == 0) ? even : odd);
            }
            else if (d[i] == 0x0A && (i == 0 || d[i - 1] != 0x0D))
            {
                return false;       // an LF alone
            }
        }
        const auto pairs = even + odd;
        return pairs >= 8 && std::min (even, odd) * 4 >= pairs;
    }

    const char* blockName (BlockType t)
    {
        switch (t)
        {
            case BlockType::Waveforms: return "waveform";
            case BlockType::Voices:    return "voice";
            case BlockType::Sequences: return "sequence";
        }
        return "unknown";
    }
}

//==============================================================================
int WaveBank::getHarmonic (int table, int harmonic) const
{
    if (table < 0 || table >= kNumWaveTables || harmonic < 0 || harmonic >= kNumHarmonics)
        return 0;
    const auto o = (std::size_t) (table * kWaveTableSize + harmonic * 2);
    return raw[o] | (raw[o + 1] << 4);
}

void WaveBank::setHarmonic (int table, int harmonic, int value)
{
    if (table < 0 || table >= kNumWaveTables || harmonic < 0 || harmonic >= kNumHarmonics)
        return;
    value = std::clamp (value, 0, 255);
    const auto o = (std::size_t) (table * kWaveTableSize + harmonic * 2);
    raw[o]     = (std::uint8_t) (value & 0x0F);
    raw[o + 1] = (std::uint8_t) ((value >> 4) & 0x0F);
}

//==============================================================================
bool parseMessage (const std::uint8_t* data, std::size_t size, DumpContents& out, std::string& error)
{
    if (data == nullptr || size < 4)
        return error = "Message too short.", false;

    if (data[0] != kSysexStart)
        return error = "Not a SysEx message (does not start with F0).", false;

    if (data[size - 1] != kSysexEnd)
        return error = "SysEx message is truncated (no F7 at the end).", false;

    if (data[1] != kManufacturerId)
    {
        char buf[96];
        std::snprintf (buf, sizeof (buf), "Not OSCar data (manufacturer ID %02X, expected 2B).", data[1]);
        return error = buf, false;
    }

    for (std::size_t i = 2; i + 1 < size; ++i)
        if (data[i] >= 0x80)
            return error = "Corrupt SysEx: unexpected status byte inside the message.", false;

    DumpContents result;
    std::size_t pos = 2;
    const std::size_t end = size - 1; // index of F7

    if (pos >= end)
        return error = "SysEx message contains no data.", false;

    while (pos < end)
    {
        const auto typeByte = data[pos++];
        const auto type = (BlockType) typeByte;

        std::size_t need = 0;
        if (type == BlockType::Waveforms)      need = kWaveBankSize;
        else if (type == BlockType::Voices)    need = kVoiceBankSize;
        else if (type == BlockType::Sequences) need = end - pos; // runs to the end of the message
        else
        {
            char buf[96];
            std::snprintf (buf, sizeof (buf), "Unknown OSCar data type %d.", (int) typeByte);
            return error = buf, false;
        }

        if (end - pos < need || need == 0)
            return error = std::string ("Incomplete ") + blockName (type) + " data (wrong length).", false;

        const auto* block = data + pos;

        for (std::size_t i = 0; i < need; ++i)
            if (! isNibble (block[i]))
                return error = std::string ("Corrupt ") + blockName (type) + " data (value above 0F).", false;

        if (looksLikeCrLfDamage (block, need))
        {
            result.suspectBlocks.push_back (type);
            result.warnings.push_back (std::string ("The ") + blockName (type)
                                       + " data looks damaged by a text-mode file copy (CR/LF), so it was not loaded.");
        }

        if (type == BlockType::Waveforms)
        {
            if (result.waves) return error = "Two waveform blocks in one message.", false;
            WaveBank w;
            std::copy (block, block + need, w.raw.begin());
            result.waves = w;
        }
        else if (type == BlockType::Voices)
        {
            if (result.voices) return error = "Two voice blocks in one message.", false;
            VoiceBank v;
            for (int i = 0; i < kNumVoices; ++i)
                std::copy (block + i * kVoiceSize, block + (i + 1) * kVoiceSize, v.voices[(std::size_t) i].begin());
            result.voices = v;
        }
        else
        {
            result.sequences = std::vector<std::uint8_t> (block, block + need);
        }

        pos += need;
    }

    out = std::move (result);
    return true;
}

bool parseStream (const std::uint8_t* data, std::size_t size, DumpContents& out, std::string& error)
{
    if (data == nullptr || size == 0)
        return error = "The file is empty.", false;

    DumpContents acc;
    std::size_t pos = 0;
    int messageCount = 0;

    while (pos < size)
    {
        if (data[pos] != kSysexStart)
            return error = "The file contains data outside SysEx messages.", false;

        std::size_t endPos = pos + 1;
        while (endPos < size && data[endPos] != kSysexEnd)
        {
            if (data[endPos] >= 0x80)
                return error = "Corrupt SysEx: a message is not terminated.", false;
            ++endPos;
        }

        if (endPos >= size)
            return error = "The file ends in the middle of a SysEx message.", false;

        DumpContents one;
        std::string msgError;
        if (! parseMessage (data + pos, endPos - pos + 1, one, msgError))
        {
            error = "Message " + std::to_string (messageCount + 1) + ": " + msgError;
            return false;
        }

        if ((one.voices && acc.voices) || (one.waves && acc.waves) || (one.sequences && acc.sequences))
            return error = "The file contains the same kind of OSCar data twice.", false;

        if (one.voices)    acc.voices    = one.voices;
        if (one.waves)     acc.waves     = one.waves;
        if (one.sequences) acc.sequences = one.sequences;
        acc.warnings.insert (acc.warnings.end(), one.warnings.begin(), one.warnings.end());
        acc.suspectBlocks.insert (acc.suspectBlocks.end(), one.suspectBlocks.begin(), one.suspectBlocks.end());

        ++messageCount;
        pos = endPos + 1;
    }

    out = std::move (acc);
    return true;
}

//==============================================================================
namespace
{
    std::vector<std::uint8_t> wrap (BlockType type, const std::uint8_t* d, std::size_t n)
    {
        std::vector<std::uint8_t> m;
        m.reserve (n + 4);
        m.push_back (kSysexStart);
        m.push_back (kManufacturerId);
        m.push_back ((std::uint8_t) type);
        for (std::size_t i = 0; i < n; ++i)
            m.push_back ((std::uint8_t) (d[i] & 0x0F)); // never emit anything but a nibble
        m.push_back (kSysexEnd);
        return m;
    }
}

std::vector<std::uint8_t> buildVoiceMessage (const VoiceBank& bank)
{
    std::vector<std::uint8_t> flat;
    flat.reserve (kVoiceBankSize);
    for (const auto& v : bank.voices)
        flat.insert (flat.end(), v.begin(), v.end());
    return wrap (BlockType::Voices, flat.data(), flat.size());
}

std::vector<std::uint8_t> buildWaveMessage (const WaveBank& w)
{
    return wrap (BlockType::Waveforms, w.raw.data(), w.raw.size());
}

std::vector<std::uint8_t> buildSequenceMessage (const std::vector<std::uint8_t>& raw)
{
    return wrap (BlockType::Sequences, raw.data(), raw.size());
}

std::vector<std::uint8_t> buildFile (const DumpContents& d)
{
    std::vector<std::uint8_t> out;
    auto append = [&out] (const std::vector<std::uint8_t>& m) { out.insert (out.end(), m.begin(), m.end()); };
    if (d.waves)     append (buildWaveMessage (*d.waves));
    if (d.voices)    append (buildVoiceMessage (*d.voices));
    if (d.sequences && ! d.sequences->empty()) append (buildSequenceMessage (*d.sequences));
    return out;
}

//==============================================================================
int slotOscarNumber (int index)
{
    if (index < 0 || index >= kNumVoices) return 0;
    return index < 12 ? -(index + 1) : index - 11;
}

std::string slotLabel (int index)
{
    return std::to_string (slotOscarNumber (index));
}

int slotAtPosition (int position)
{
    if (position < 0 || position >= kNumVoices) return -1;
    return position < 12 ? 11 - position : position;
}

int positionOfSlot (int index)
{
    return slotAtPosition (index);
}

int slotIndexFromLabel (int n)
{
    if (n <= -1 && n >= -12) return -n - 1;
    if (n >= 1 && n <= 24)   return n + 11;
    return -1;
}

} // namespace oscar
