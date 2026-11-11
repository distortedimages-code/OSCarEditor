#include "OscarParams.h"

#include <algorithm>
#include <cstdlib>

namespace oscar
{
namespace
{
    constexpr int kFunctionNibble = 56, kFilterTypeNibble = 58, kTriggerNibble = 59, kArpNibble = 62;

    // Values the firmware's dump routine sends for six-way switches (table at 0x5B80)
    // and for semitones (table at 0x5C00).
    constexpr int kSixDump[6]      = { 10, 31, 52, 73, 94, 115 };
    constexpr int kSemitoneDump[16] = { 116, 108, 100, 92, 84, 76, 68, 60, 52, 44, 36, 28, 20, 12, 4, 60 };
    constexpr int kSparseValues[6] = { 0, 1, 2, 4, 5, 6 };

    ParamDef knob (const char* id, const char* name, const char* section, int index, int cc)
    {
        return { id, name, section, ParamKind::Knob, index * 2, 0, 0, {}, true, cc, CcCoding::Knob };
    }

    ParamDef sw (const char* id, const char* name, const char* section, int offset, std::uint8_t mask, int shift,
                 std::vector<Choice> choices, bool verified, int cc, CcCoding coding)
    {
        return { id, name, section, ParamKind::Switch, offset, mask, shift, std::move (choices), verified, cc, coding };
    }

    std::vector<Choice> labelled (std::initializer_list<int> values, std::initializer_list<const char*> labels)
    {
        std::vector<Choice> c;
        auto l = labels.begin();
        for (int v : values)
            c.push_back ({ v, l != labels.end() ? *l++ : std::to_string (v) });
        return c;
    }

    std::vector<Choice> numbered (int from, int to)
    {
        std::vector<Choice> c;
        for (int v = from; v <= to; ++v)
            c.push_back ({ v, std::to_string (v) });
        return c;
    }

    std::vector<Choice> semitones()
    {
        // Tauntek chart: value 0 = +7, 7 = 0, 14 = -7
        std::vector<Choice> c;
        for (int v = 0; v <= 14; ++v)
        {
            const int st = 7 - v;
            c.push_back ({ v, st > 0 ? "+" + std::to_string (st) : std::to_string (st) });
        }
        return c;
    }

    std::vector<Choice> waveforms (const char* five)
    {
        const char* names[16] = { "Triangle", "Sawtooth", "Square", "Pulse", "PWM", five,
                                  "Wave -3", "Wave -4", "Wave -5", "Wave -6", "Wave -7",
                                  "User -8", "User -9", "User -10", "User -11", "User -12" };
        std::vector<Choice> c;
        for (int v = 0; v < 16; ++v)
            c.push_back ({ v, names[v] });
        return c;
    }

    std::vector<ParamDef> makeParams()
    {
        std::vector<ParamDef> p;

        // --- 26 knobs, wire order = edit buffer 0x6024..0x603D; CC numbers from the firmware ---
        p.push_back (knob ("filterDrive",  "Drive",           "Filter",      0,  29));
        p.push_back (knob ("oscBalance",   "Osc Balance",     "Oscillators", 1,  27));
        p.push_back (knob ("noiseBalance", "Noise",           "Oscillators", 2,  28));
        p.push_back (knob ("filterFreq",   "Frequency",       "Filter",      3,  30));
        p.push_back (knob ("filterSep",    "Separation",      "Filter",      4,  31));
        p.push_back (knob ("volSustain",   "Sustain",         "Amp Env",     5,  43));
        p.push_back (knob ("bendAmount",   "Bend Amount",     "Wheels",      6,  26));
        p.push_back (knob ("mwPitch",      "Mod > Pitch",     "Wheels",      7,  25));
        p.push_back (knob ("mwFilter",     "Mod > Filter",    "Wheels",      8,  40));
        p.push_back (knob ("pulseWidth",   "Pulse Width",     "Oscillators", 9,  16));
        p.push_back (knob ("resonance",    "Q",               "Filter",      10, 32));
        p.push_back (knob ("filtSustain",  "Sustain",         "Filter Env",  11, 36));
        p.push_back (knob ("detune",       "Detune",          "Oscillators", 12, 17));
        p.push_back (knob ("lfoFilter",    "Direct > Filter", "Modulation",  13, 38));
        p.push_back (knob ("envFilter",    "Env Amount",      "Filter",      14, 39));
        p.push_back (knob ("lfoPitch",     "Direct > Pitch",  "Modulation",  15, 24));
        p.push_back (knob ("gateTime",     "Gate Time",       "Performance", 16, 45));
        p.push_back (knob ("volAttack",    "Attack",          "Amp Env",     17, 41));
        p.push_back (knob ("volRelease",   "Release",         "Amp Env",     18, 44));
        p.push_back (knob ("volDecay",     "Decay",           "Amp Env",     19, 42));
        p.push_back (knob ("filtAttack",   "Attack",          "Filter Env",  20, 34));
        p.push_back (knob ("filtRelease",  "Release",         "Filter Env",  21, 37));
        p.push_back (knob ("filtDecay",    "Decay",           "Filter Env",  22, 35));
        p.push_back (knob ("glideRate",    "Glide Rate",      "Performance", 23, 23));
        p.push_back (knob ("lfoRate",      "LFO Rate",        "Modulation",  24, 47));
        p.push_back (knob ("introDelay",   "Intro Delay",     "Modulation",  25, 48));

        // --- switches: nibbles 52..63 = edit buffer 0x609C..0x60A7 ---
        p.push_back (sw ("wave1",       "Wave 1",        "Oscillators", 52, 0x0F, 0, waveforms ("Off"), false, 14, CcCoding::Steps16));
        p.push_back (sw ("glideType",   "Glide Type",    "Performance", 53, 0x07, 0,
                         labelled ({ 0, 1, 2, 4, 5, 6 }, { "Normal", "Auto", "Gliss", "Norm fix", "Auto fix", "Gliss fix" }),
                         true, 22, CcCoding::SixSparse));
        p.push_back (sw ("octaveShift", "Osc 2 Octave",  "Oscillators", 54, 0x07, 0,
                         labelled ({ 0, 1, 2, 3, 4, 5 }, { "-2", "-1", "0", "+1", "+2", "+3" }), true, 18, CcCoding::Six));
        p.push_back (sw ("wave2",       "Wave 2",        "Oscillators", 55, 0x0F, 0, waveforms ("Osc 1"), false, 15, CcCoding::Steps16));
        p.push_back (sw ("function",    "Function",      "Performance", 56, 0x07, 0,
                         labelled ({ 0, 1, 2, 4, 5, 6 }, { "Normal", "Arp", "Duo", "Norm hold", "Arp hold", "Duo hold" }),
                         true, 50, CcCoding::SixSparse));
        p.push_back (sw ("lfoToOsc1",   "LFO > Osc 1",   "Tauntek",     56, 0x08, 3,
                         labelled ({ 0, 1 }, { "On", "Off" }), true, 52, CcCoding::Two));
        p.push_back (sw ("lfoWave",     "LFO Wave",      "Modulation",  57, 0x07, 0,
                         labelled ({ 0, 1, 2, 3, 4, 5 }, { "Triangle", "Sawtooth", "Square", "Filt env", "Keyboard", "Random" }),
                         true, 46, CcCoding::Six));
        p.push_back (sw ("lfoSync",     "LFO Sync",      "Tauntek",     57, 0x08, 3,
                         labelled ({ 0, 1 }, { "Off", "On" }), true, 53, CcCoding::Two));
        p.push_back (sw ("filterType",  "Type",          "Filter",      58, 0x07, 0,
                         labelled ({ 0, 1, 2, 4, 5, 6 }, { "LP", "BP", "HP", "LP no kb", "BP no kb", "HP no kb" }),
                         true, 33, CcCoding::SixSparse));
        p.push_back (sw ("trigger",     "Trigger",       "Performance", 59, 0x07, 0,
                         labelled ({ 0, 1, 2, 3, 4, 5 }, { "Single", "Multi", "Rpt 1", "Rpt 2", "Rpt 1+2", "Ext" }),
                         true, 49, CcCoding::Six));
        p.push_back ({ "pwmRate", "PWM Rate", "Tauntek", ParamKind::PwmRate, kFilterTypeNibble, 0x08, 3,
                       labelled ({ 0, 1, 2, 3 }, { "Slowest", "Slower", "Normal", "Fastest" }), true, 55, CcCoding::PwmRate });
        p.push_back (sw ("tuneSemi",    "Tune Semi",     "Oscillators", 60, 0x0F, 0, semitones(), true, 20, CcCoding::Semitone));
        p.push_back (sw ("osc2Semi",    "Osc 2 Semi",    "Oscillators", 61, 0x0F, 0, semitones(), true, 19, CcCoding::Semitone));
        p.push_back (sw ("arpDir",      "Arp Direction", "Performance", 62, 0x03, 0,
                         labelled ({ 1, 2, 3 }, { "Down", "Up", "Up/Dn" }), false, 51, CcCoding::ArpNibble));
        p.push_back (sw ("arpMode",     "Arp Mode",      "Performance", 62, 0x0C, 2,
                         labelled ({ 0, 1, 2 }, { "Normal", "Delete", "Memory" }), false, 51, CcCoding::ArpNibble));
        p.push_back (sw ("octaveSet",   "Octave",        "Performance", 63, 0x07, 0, numbered (2, 6), false, 21, CcCoding::OctaveSet));

        return p;
    }

    int pwmFlags (const VoiceData& v)
    {
        const bool a = (v[(size_t) kFilterTypeNibble] & 0x08) != 0;
        const bool b = (v[(size_t) kTriggerNibble] & 0x08) != 0;
        if (a && b) return 0;   // slowest
        if (a)      return 1;   // slower
        if (! b)    return 2;   // normal (also what pre-Tauntek voices hold)
        return 3;               // fastest
    }
} // namespace

// Exact copy of the firmware's patch-dump encoding (routine at 0x59F2).
int dumpCcFor (CcCoding coding, int value)
{
    switch (coding)
    {
        case CcCoding::Knob:      return (value & 0xFF) >> 1;
        case CcCoding::Steps16:
        case CcCoding::ArpNibble: return (value & 0x0F) * 8 + 4;
        case CcCoding::Semitone:  return kSemitoneDump[value & 0x0F];
        case CcCoding::Six:       { const int i = value & 0xFF; return kSixDump[i < 6 ? i : 0]; }
        case CcCoding::SixSparse: { int i = value & 0xFF; if (i >= 3) --i; return kSixDump[i < 6 ? i : 0]; }
        case CcCoding::OctaveSet: { int a = (value - 2) & 0xFF; if (a >= 5) a = 2; return 12 + 25 * a; }
        case CcCoding::Two:       return value ? 64 : 0;
        case CcCoding::PwmRate:   { constexpr int t[4] = { 16, 48, 80, 112 }; return t[value & 3]; }
        case CcCoding::None:      break;
    }
    return -1;
}

std::vector<std::pair<int, std::uint8_t>> ownedBits (const ParamDef& p)
{
    switch (p.kind)
    {
        case ParamKind::Knob:    return { { p.offset, 0x0F }, { p.offset + 1, 0x0F } };
        case ParamKind::Switch:  return { { p.offset, p.mask } };
        case ParamKind::PwmRate: return { { kFilterTypeNibble, 0x08 }, { kTriggerNibble, 0x08 } };
    }
    return {};
}

const std::vector<ParamDef>& allParams()
{
    static const std::vector<ParamDef> params = makeParams();
    return params;
}

const ParamDef* findParam (const std::string& id)
{
    for (const auto& p : allParams())
        if (id == p.id)
            return &p;
    return nullptr;
}

int maxParamValue (const ParamDef& p)
{
    switch (p.kind)
    {
        case ParamKind::Knob:    return kKnobMaxEditable;
        case ParamKind::Switch:  return p.mask >> p.shift;
        case ParamKind::PwmRate: return 3;
    }
    return 0;
}

bool isValidChoice (const ParamDef& p, int value)
{
    for (const auto& c : p.choices)
        if (c.value == value)
            return true;
    return false;
}

std::string choiceLabel (const ParamDef& p, int value)
{
    for (const auto& c : p.choices)
        if (c.value == value)
            return c.label;
    return "Value " + std::to_string (value);
}

int getParam (const VoiceData& v, const ParamDef& p)
{
    const auto o = (std::size_t) p.offset;
    switch (p.kind)
    {
        case ParamKind::Knob:    return (v[o] & 0x0F) | ((v[o + 1] & 0x0F) << 4);
        case ParamKind::Switch:  return (v[o] & p.mask) >> p.shift;
        case ParamKind::PwmRate: return pwmFlags (v);
    }
    return 0;
}

bool setParam (VoiceData& v, const ParamDef& p, int value)
{
    if (value < 0 || value > maxParamValue (p))
        return false;

    const auto o = (std::size_t) p.offset;
    switch (p.kind)
    {
        case ParamKind::Knob:
            v[o]     = (std::uint8_t) (value & 0x0F);
            v[o + 1] = (std::uint8_t) ((value >> 4) & 0x0F);
            break;

        case ParamKind::Switch:
        {
            const auto bits = (std::uint8_t) ((value << p.shift) & p.mask);
            v[o] = (std::uint8_t) (((v[o] & ~p.mask) | bits) & 0x0F);
            break;
        }

        case ParamKind::PwmRate:
        {
            const bool a = value <= 1;               // slowest, slower
            const bool b = value == 0 || value == 3; // slowest, fastest
            auto& f = v[(size_t) kFilterTypeNibble];
            auto& t = v[(size_t) kTriggerNibble];
            f = (std::uint8_t) (a ? (f | 0x08) : (f & 0x07));
            t = (std::uint8_t) (b ? (t | 0x08) : (t & 0x07));
            break;
        }
    }
    return true;
}

//==============================================================================
int ccValueFor (CcCoding coding, int value)
{
    switch (coding)
    {
        case CcCoding::Knob:      return (value >= 0 && value <= 255) ? value >> 1 : -1;
        case CcCoding::Steps16:
        case CcCoding::ArpNibble: return (value >= 0 && value <= 15) ? value * 8 + 4 : -1;
        case CcCoding::Semitone:  return (value >= 0 && value <= 14) ? (14 - value) * 8 + 4 : -1;
        case CcCoding::Six:       return (value >= 0 && value <= 5) ? kSixDump[value] : -1;
        case CcCoding::SixSparse:
            for (int i = 0; i < 6; ++i)
                if (kSparseValues[i] == value)
                    return kSixDump[i];
            return -1;
        case CcCoding::OctaveSet: return (value >= 2 && value <= 6) ? 12 + 25 * (value - 2) : -1;
        case CcCoding::Two:       return value == 0 ? 0 : (value == 1 ? 64 : -1);
        case CcCoding::PwmRate:   return (value >= 0 && value <= 3) ? 16 + 32 * value : -1;
        case CcCoding::None:      break;
    }
    return -1;
}

int firmwareValueFromCc (CcCoding coding, int cc)
{
    cc = std::clamp (cc, 0, 127);
    switch (coding)
    {
        case CcCoding::Knob:      return std::max (1, cc * 2);                 // 0x4224: add a,a / inc if zero
        case CcCoding::Steps16:
        case CcCoding::ArpNibble: return (cc >> 3) & 0x0F;                    // 0x4200: rrca x3, and 0Fh
        case CcCoding::Semitone:  return 14 - std::min (cc >> 3, 14);          // table 0x4780
        case CcCoding::Six:       return std::min (cc / 21, 5);                // table 0x4580
        case CcCoding::SixSparse: return kSparseValues[std::min (cc / 21, 5)]; // table 0x4600
        case CcCoding::OctaveSet: return 2 + std::min (cc / 25, 4);            // table 0x4680
        case CcCoding::Two:       return (cc & 0x40) ? 1 : 0;                  // 0x44AB: bit 6
        case CcCoding::PwmRate:   return cc >> 5;                              // 0x44D1: bits 6, 5
        case CcCoding::None:      break;
    }
    return -1;
}

std::vector<CcMessage> ccMessagesFor (const ParamDef& p, const VoiceData& v)
{
    std::vector<CcMessage> out;
    if (p.cc < 0 || p.coding == CcCoding::None)
        return out;

    if (p.coding == CcCoding::ArpNibble)
    {
        out.emplace_back (p.cc, ccValueFor (CcCoding::ArpNibble, v[(size_t) kArpNibble] & 0x0F));
        // CC 51 also sets Function bit 2 from arp bit 3: restore the stored Function.
        const int f = ccValueFor (CcCoding::SixSparse, v[(size_t) kFunctionNibble] & 0x07);
        if (f >= 0)
            out.emplace_back (50, f);
        return out;
    }

    const int c = ccValueFor (p.coding, getParam (v, p));
    if (c >= 0)
        out.emplace_back (p.cc, c);
    return out;
}

std::vector<std::string> compareWithCcDump (const VoiceData& v, const std::vector<int>& ccValues)
{
    std::vector<std::string> differ;
    bool arpDone = false;

    for (const auto& p : allParams())
    {
        if (p.cc < 0 || p.cc >= (int) ccValues.size() || ccValues[(size_t) p.cc] < 0)
            continue;

        int value = 0;
        if (p.coding == CcCoding::ArpNibble)
        {
            if (arpDone) continue;
            arpDone = true;
            value = v[(size_t) kArpNibble] & 0x0F;
        }
        else
        {
            value = getParam (v, p);
        }

        if (dumpCcFor (p.coding, value) != ccValues[(size_t) p.cc])
            differ.push_back (p.coding == CcCoding::ArpNibble ? std::string ("Arpeggiator")
                                                              : std::string (p.section) + " " + p.name);
    }
    return differ;
}

CcDumpRecall voiceFromCcDump (const VoiceData& reference, const std::vector<int>& ccValues)
{
    CcDumpRecall r;
    r.voice = reference;
    bool arpDone = false;

    for (const auto& p : allParams())
    {
        if (p.cc < 0 || p.cc >= (int) ccValues.size() || ccValues[(size_t) p.cc] < 0)
            continue;

        const int d = ccValues[(size_t) p.cc];

        if (p.coding == CcCoding::ArpNibble)
        {
            if (arpDone)
                continue;
            arpDone = true;
            const int cur = reference[(size_t) kArpNibble] & 0x0F;
            if (dumpCcFor (CcCoding::ArpNibble, cur) == d)
                continue;
            int found = -1;
            for (int n = 0; n < 16 && found < 0; ++n)
                if (dumpCcFor (CcCoding::ArpNibble, n) == d)
                    found = n;
            if (found < 0)
            {
                r.unreadable.push_back ("Arpeggiator");
                continue;
            }
            r.voice[(size_t) kArpNibble] = (std::uint8_t) found;
            r.changed.push_back ("Arpeggiator");
            continue;
        }

        const int cur = getParam (reference, p);
        if (dumpCcFor (p.coding, cur) == d)
            continue;

        int found = -1;
        if (p.coding == CcCoding::Knob)
        {
            if (d <= 127)
            {
                // the dump drops the lowest bit: take the candidate nearest to the reference
                const int a = std::min (d * 2, kKnobMaxEditable);
                const int b = std::min (d * 2 + 1, kKnobMaxEditable);
                found = std::abs (a - cur) <= std::abs (b - cur) ? a : b;
            }
        }
        else
        {
            for (const auto& c : p.choices)
                if (found < 0 && dumpCcFor (p.coding, c.value) == d)
                    found = c.value;
        }

        const auto name = std::string (p.section) + " " + p.name;
        if (found < 0 || ! setParam (r.voice, p, found))
        {
            r.unreadable.push_back (name);
            continue;
        }
        r.changed.push_back (name);
    }
    return r;
}

//==============================================================================
VoiceData initVoice()
{
    // knobs: 1 = fastest / closed / none, 254 = slowest / fully open; the bipolar ones
    // (Detune, Env Amount, Direct > Pitch, Direct > Filter) are at zero around 128
    static const std::pair<const char*, int> values[] = {
        { "wave1", 1 }, { "wave2", 1 }, { "octaveShift", 2 }, { "tuneSemi", 7 }, { "osc2Semi", 7 },
        { "oscBalance", 1 }, { "detune", 136 }, { "noiseBalance", 1 }, { "pulseWidth", 128 }, { "pwmRate", 2 },
        { "filterType", 0 }, { "filterFreq", 254 }, { "resonance", 1 }, { "filterSep", 1 }, { "filterDrive", 176 },
        { "envFilter", 128 },
        { "filtAttack", 1 }, { "filtDecay", 120 }, { "filtSustain", 1 }, { "filtRelease", 110 },
        { "volAttack", 1 }, { "volDecay", 128 }, { "volSustain", 254 }, { "volRelease", 60 },
        { "lfoWave", 0 }, { "lfoRate", 192 }, { "introDelay", 1 }, { "lfoPitch", 128 }, { "lfoFilter", 128 },
        { "lfoToOsc1", 0 }, { "lfoSync", 0 },
        { "bendAmount", 92 }, { "mwPitch", 32 }, { "mwFilter", 1 }, { "octaveSet", 4 },
        { "function", 0 }, { "trigger", 1 }, { "glideType", 0 }, { "glideRate", 1 },
        { "arpDir", 3 }, { "arpMode", 1 }, { "gateTime", 96 },
    };
    VoiceData v {};
    for (const auto& [id, value] : values)
        if (const auto* p = findParam (id))
            setParam (v, *p, value);
    return v;
}

VoiceData voiceAsSentOverCc (const VoiceData& v)
{
    auto out = v;
    for (const auto& p : allParams())
        if (p.cc >= 0 && p.coding == CcCoding::Knob)
        {
            const int cc = ccValueFor (p.coding, getParam (v, p));
            if (cc >= 0)
                setParam (out, p, firmwareValueFromCc (p.coding, cc));
        }
    return out;
}

} // namespace oscar
