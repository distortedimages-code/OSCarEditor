// Unit tests for the OSCar SysEx codec and parameter map. No framework needed.
// Run: CoreTests <path to factory_codemechanic.syx>

#include "../Source/core/FactoryBank.h"
#include "../Source/core/OscarParams.h"
#include "../Source/core/OscarSysex.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <algorithm>

using namespace oscar;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static std::vector<std::uint8_t> readFile (const char* path)
{
    std::ifstream f (path, std::ios::binary);
    return { std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>() };
}

static VoiceBank factoryBank()
{
    VoiceBank b;
    for (int i = 0; i < kNumVoices; ++i)
        for (int k = 0; k < kVoiceSize; ++k)
            b.voices[(size_t) i][(size_t) k] = factory::kVoiceBank[i * kVoiceSize + k];
    return b;
}

static void testFactoryFile (const char* path)
{
    const auto file = readFile (path);
    CHECK (file.size() == 2552);

    DumpContents d;
    std::string err;
    CHECK (parseStream (file, d, err));
    CHECK (d.voices.has_value());
    CHECK (d.waves.has_value());
    CHECK (! d.sequences.has_value());
    // the archive's waveform block is CR/LF-damaged: must be flagged
    CHECK (d.warnings.size() == 1);
    CHECK (d.isSuspect (BlockType::Waveforms));
    CHECK (! d.isSuspect (BlockType::Voices));

    // embedded factory header == voice block of the file
    CHECK (*d.voices == factoryBank());

    // rebuild is byte-identical to the original messages
    const auto voiceMsg = buildVoiceMessage (*d.voices);
    const auto waveMsg  = buildWaveMessage (*d.waves);
    std::vector<std::uint8_t> rebuilt (waveMsg);
    rebuilt.insert (rebuilt.end(), voiceMsg.begin(), voiceMsg.end());
    CHECK (rebuilt == file);
    CHECK (buildFile (d) == file);
    CHECK (voiceMsg.size() == 2308);
}

static void testSlots()
{
    std::set<int> seen;
    for (int i = 0; i < kNumVoices; ++i)
    {
        const int n = slotOscarNumber (i);
        CHECK (n != 0 && n >= -12 && n <= 24);
        CHECK (slotIndexFromLabel (n) == i);
        seen.insert (n);
    }
    CHECK (seen.size() == 36);
    CHECK (slotLabel (0) == "-1");
    CHECK (slotLabel (11) == "-12");
    CHECK (slotLabel (12) == "1");
    CHECK (slotLabel (35) == "24");
    CHECK (slotIndexFromLabel (0) == -1);
    CHECK (slotIndexFromLabel (25) == -1);
    CHECK (slotIndexFromLabel (-13) == -1);

    // shown as on the keyboard: -12 .. -1, then 1 .. 24
    std::string order;
    for (int pos = 0; pos < 36; ++pos)
    {
        CHECK (positionOfSlot (slotAtPosition (pos)) == pos);
        order += slotLabel (slotAtPosition (pos)) + " ";
    }
    CHECK (order.rfind ("-12 -11 -10 -9 -8 -7 -6 -5 -4 -3 -2 -1 1 2 3 ", 0) == 0);
    CHECK (slotAtPosition (35) == 35 && slotAtPosition (36) == -1 && slotAtPosition (-1) == -1);
}

static void testParamMapLayout()
{
    const auto& params = allParams();
    CHECK (params.size() == 26 + 16);

    // every wire bit of a voice is owned by at most one parameter, and the
    // 26 knobs cover bytes 0..51 completely
    std::array<std::uint8_t, kVoiceSize> owned {};
    for (const auto& p : params)
    {
        for (const auto& [offset, mask] : ownedBits (p))
        {
            CHECK (offset >= 0 && offset < kVoiceSize);
            CHECK ((mask & 0xF0) == 0);
            CHECK ((owned[(size_t) offset] & mask) == 0);
            owned[(size_t) offset] |= mask;
        }

        if (p.kind == ParamKind::Knob)
        {
            CHECK (p.offset % 2 == 0 && p.offset + 1 < 52);
        }
        else
        {
            CHECK (p.offset >= 52 && p.offset < 64);
            CHECK (! p.choices.empty());
            for (const auto& c : p.choices)
                CHECK (c.value >= 0 && c.value <= maxParamValue (p) && ! c.label.empty());
        }
        CHECK (findParam (p.id) == &p);
    }
    for (int k = 0; k < 52; ++k)
        CHECK (owned[(size_t) k] == 0x0F);

    // Tauntek uses bit 3 of the Function, LFO Wave, Filter Type and Trigger nibbles
    CHECK (owned[56] == 0x0F && owned[57] == 0x0F && owned[58] == 0x0F && owned[59] == 0x0F);
}

// Changing one parameter must change only that parameter's bits.
static void testEditIsolation()
{
    const auto bank = factoryBank();
    for (int vi = 0; vi < kNumVoices; ++vi)
    {
        const auto& orig = bank.voices[(size_t) vi];
        for (const auto& p : allParams())
        {
            const int cur = getParam (orig, p);
            const auto bits = ownedBits (p);

            // writing back the current value changes nothing (when editable)
            if (cur <= maxParamValue (p))
            {
                auto v = orig;
                CHECK (setParam (v, p, cur));
                CHECK (v == orig);
            }

            for (int value = 0; value <= maxParamValue (p); ++value)
            {
                auto v = orig;
                CHECK (setParam (v, p, value));
                CHECK (getParam (v, p) == value);

                for (int k = 0; k < kVoiceSize; ++k)
                {
                    CHECK (v[(size_t) k] <= 0x0F);
                    std::uint8_t mine = 0;
                    for (const auto& [o, m] : bits)
                        if (o == k) mine |= m;
                    CHECK ((v[(size_t) k] & ~mine & 0x0F) == (orig[(size_t) k] & ~mine & 0x0F));
                }

                // other parameters still read the same
                for (const auto& q : allParams())
                    if (&q != &p)
                        CHECK (getParam (v, q) == getParam (orig, q));
            }

            // out of range is refused and leaves the data alone
            auto v = orig;
            CHECK (! setParam (v, p, maxParamValue (p) + 1));
            CHECK (! setParam (v, p, -1));
            CHECK (v == orig);
        }
    }
}

// Tauntek chart (Oscarccmap.pdf, Feb 2021): CC number for each parameter.
static void testCcNumbers()
{
    const std::pair<const char*, int> chart[] = {
        { "wave1", 14 }, { "wave2", 15 }, { "pulseWidth", 16 }, { "detune", 17 }, { "octaveShift", 18 },
        { "osc2Semi", 19 }, { "tuneSemi", 20 }, { "octaveSet", 21 }, { "glideType", 22 }, { "glideRate", 23 },
        { "lfoPitch", 24 }, { "mwPitch", 25 }, { "bendAmount", 26 }, { "oscBalance", 27 }, { "noiseBalance", 28 },
        { "filterDrive", 29 }, { "filterFreq", 30 }, { "filterSep", 31 }, { "resonance", 32 }, { "filterType", 33 },
        { "filtAttack", 34 }, { "filtDecay", 35 }, { "filtSustain", 36 }, { "filtRelease", 37 }, { "lfoFilter", 38 },
        { "envFilter", 39 }, { "mwFilter", 40 }, { "volAttack", 41 }, { "volDecay", 42 }, { "volSustain", 43 },
        { "volRelease", 44 }, { "gateTime", 45 }, { "lfoWave", 46 }, { "lfoRate", 47 }, { "introDelay", 48 },
        { "trigger", 49 }, { "function", 50 }, { "arpDir", 51 }, { "arpMode", 51 }, { "lfoToOsc1", 52 },
        { "lfoSync", 53 }, { "pwmRate", 55 } };

    CHECK (sizeof (chart) / sizeof (chart[0]) == allParams().size());
    for (const auto& [id, cc] : chart)
    {
        const auto* p = findParam (id);
        CHECK (p != nullptr && p->cc == cc);
    }
}

// Lookup tables read from the firmware image (CC value ranges -> stored value).
static int tableFromFirmware (CcCoding coding, int cc)
{
    struct Run { int from, to, value; };
    static const std::vector<Run> six    { { 0, 20, 0 }, { 21, 41, 1 }, { 42, 62, 2 }, { 63, 83, 3 }, { 84, 104, 4 }, { 105, 127, 5 } };
    static const std::vector<Run> sparse { { 0, 20, 0 }, { 21, 41, 1 }, { 42, 62, 2 }, { 63, 83, 4 }, { 84, 104, 5 }, { 105, 127, 6 } };
    static const std::vector<Run> octave { { 0, 24, 2 }, { 25, 49, 3 }, { 50, 74, 4 }, { 75, 99, 5 }, { 100, 127, 6 } };
    static const std::vector<Run> semi   { { 0, 7, 14 }, { 8, 15, 13 }, { 16, 23, 12 }, { 24, 31, 11 }, { 32, 39, 10 },
                                           { 40, 47, 9 }, { 48, 55, 8 }, { 56, 63, 7 }, { 64, 71, 6 }, { 72, 79, 5 },
                                           { 80, 87, 4 }, { 88, 95, 3 }, { 96, 103, 2 }, { 104, 111, 1 }, { 112, 127, 0 } };
    const std::vector<Run>* t = nullptr;
    switch (coding)
    {
        case CcCoding::Six:       t = &six; break;
        case CcCoding::SixSparse: t = &sparse; break;
        case CcCoding::OctaveSet: t = &octave; break;
        case CcCoding::Semitone:  t = &semi; break;
        default: return -2;
    }
    for (const auto& r : *t)
        if (cc >= r.from && cc <= r.to)
            return r.value;
    return -3;
}

static void testCcCoding()
{
    // decoder == firmware tables, for every CC value
    for (auto coding : { CcCoding::Six, CcCoding::SixSparse, CcCoding::OctaveSet, CcCoding::Semitone })
        for (int cc = 0; cc < 128; ++cc)
            CHECK (firmwareValueFromCc (coding, cc) == tableFromFirmware (coding, cc));

    // knobs: firmware stores max (1, 2 * cc)
    CHECK (firmwareValueFromCc (CcCoding::Knob, 0) == 1);
    CHECK (firmwareValueFromCc (CcCoding::Knob, 127) == 254);

    // every value the plugin sends lands on exactly the intended value
    for (const auto& p : allParams())
    {
        if (p.kind == ParamKind::Knob)
        {
            for (int v = 0; v <= kKnobMaxEditable; ++v)
            {
                const int cc = ccValueFor (p.coding, v);
                CHECK (cc >= 0 && cc <= 127);
                CHECK (firmwareValueFromCc (p.coding, cc) == std::max (1, v & ~1));
            }
            continue;
        }

        for (const auto& c : p.choices)
        {
            const int cc = ccValueFor (p.coding, c.value);
            CHECK (cc >= 0 && cc <= 127);
            CHECK (firmwareValueFromCc (p.coding, cc) == c.value);
        }
    }

    // values the firmware can't receive are never sent
    CHECK (ccValueFor (CcCoding::SixSparse, 3) == -1);
    CHECK (ccValueFor (CcCoding::Semitone, 15) == -1);
    CHECK (ccValueFor (CcCoding::OctaveSet, 1) == -1);

    // the arpeggiator message restores Function afterwards (CC 51 overwrites Function bit 2)
    auto v = factoryBank().voices[0];
    setParam (v, *findParam ("arpMode"), 2);
    setParam (v, *findParam ("function"), 1);
    const auto msgs = ccMessagesFor (*findParam ("arpDir"), v);
    CHECK (msgs.size() == 2 && msgs[0].first == 51 && msgs[1].first == 50);
    CHECK (firmwareValueFromCc (CcCoding::ArpNibble, msgs[0].second) == (v[62] & 0x0F));
    CHECK (firmwareValueFromCc (CcCoding::SixSparse, msgs[1].second) == 1);

    // Tauntek chart examples: dump values for semitones and two-way switches
    CHECK (dumpCcFor (CcCoding::Semitone, 0) == 116 && dumpCcFor (CcCoding::Semitone, 7) == 60
           && dumpCcFor (CcCoding::Semitone, 14) == 4);
    CHECK (dumpCcFor (CcCoding::Two, 1) == 64 && dumpCcFor (CcCoding::PwmRate, 2) == 80);
}

static void testPwmRate()
{
    const auto* p = findParam ("pwmRate");
    VoiceData v {};
    CHECK (getParam (v, *p) == 2);               // pre-Tauntek voices: normal rate
    setParam (v, *p, 0); CHECK (v[58] == 0x08 && v[59] == 0x08);   // slowest: both flags
    setParam (v, *p, 1); CHECK (v[58] == 0x08 && v[59] == 0x00);   // slower
    setParam (v, *p, 3); CHECK (v[58] == 0x00 && v[59] == 0x08);   // fastest
    // the CC sent for each rate decodes to the same rate in the firmware
    for (int r = 0; r < 4; ++r)
        CHECK (firmwareValueFromCc (CcCoding::PwmRate, ccValueFor (CcCoding::PwmRate, r)) == r);
}

// A patch dump the OSCar would send for a voice must compare clean with that voice.
static void testDumpComparison()
{
    const auto bank = factoryBank();
    for (const auto& v : bank.voices)
    {
        std::vector<int> dump (128, -1);
        for (const auto& p : allParams())
            if (p.cc >= 0)
                dump[(size_t) p.cc] = dumpCcFor (p.coding, p.coding == CcCoding::ArpNibble ? (v[62] & 0x0F) : getParam (v, p));
        CHECK (compareWithCcDump (v, dump).empty());

        auto changed = v;
        setParam (changed, *findParam ("filterFreq"), (getParam (v, *findParam ("filterFreq")) + 2) % 254);
        const auto diff = compareWithCcDump (changed, dump);
        CHECK (diff.size() == 1 && diff[0] == "Filter Frequency");
    }

    // a missing CC is not reported as a difference
    std::vector<int> empty (128, -1);
    CHECK (compareWithCcDump (bank.voices[3], empty).empty());
}

// What the Tauntek sends for a voice (firmware routine 0x59F2).
static std::vector<int> patchDumpOf (const VoiceData& v)
{
    std::vector<int> dump (128, -1);
    for (const auto& p : allParams())
        if (p.cc >= 0)
            dump[(size_t) p.cc] = dumpCcFor (p.coding, p.coding == CcCoding::ArpNibble ? (v[62] & 0x0F) : getParam (v, p));
    return dump;
}

// Total recall: rebuild what the OSCar plays from its patch dump.
static void testRecallFromDump()
{
    const auto bank = factoryBank();
    std::mt19937 rng (7);

    // same voice as reference: nothing changes, bit for bit
    for (const auto& v : bank.voices)
    {
        const auto r = voiceFromCcDump (v, patchDumpOf (v));
        CHECK (r.voice == v);
        CHECK (r.changed.empty() && r.unreadable.empty());
    }

    // the OSCar plays voice a, the plugin believes it is voice b
    for (int a = 0; a < kNumVoices; ++a)
        for (int b = 0; b < kNumVoices; ++b)
        {
            const auto& real = bank.voices[(size_t) a];
            const auto& ref  = bank.voices[(size_t) b];
            const auto dump = patchDumpOf (real);
            const auto r = voiceFromCcDump (ref, dump);

            CHECK (compareWithCcDump (r.voice, dump).empty());
            CHECK (r.unreadable.empty());
            for (const auto& p : allParams())
            {
                const int got = getParam (r.voice, p), want = getParam (real, p);
                if (p.kind == ParamKind::Knob)
                    CHECK (std::abs (got - want) <= 1);
                else if (isValidChoice (p, want) || p.coding == CcCoding::ArpNibble)
                    CHECK (got == want
                           // an invalid reference value that the firmware dumps the same way is kept
                           || (! isValidChoice (p, getParam (ref, p)) && dumpCcFor (p.coding, got) == dumpCcFor (p.coding, want)));
                else // factory voice 35 stores octave 1, which the firmware dumps like octave 4
                    CHECK (dumpCcFor (p.coding, got) == dumpCcFor (p.coding, want));
            }
            // bits nobody owns come from the reference
            for (int n : { 53, 54, 63 })
                CHECK ((r.voice[(size_t) n] & 0x08) == (ref[(size_t) n] & 0x08));
            CHECK (r.changed.empty() == (compareWithCcDump (ref, dump).empty()));
        }

    // Function: a Duo voice recalled over a Normal one (the reported bug)
    {
        auto normal = bank.voices[12];
        setParam (normal, *findParam ("function"), 0);
        auto duo = normal;
        setParam (duo, *findParam ("function"), 2);
        const auto r = voiceFromCcDump (normal, patchDumpOf (duo));
        CHECK (getParam (r.voice, *findParam ("function")) == 2);
        CHECK (r.changed.size() == 1 && r.changed[0] == "Performance Function");
    }

    // random but valid voices, random reference
    for (int i = 0; i < 20000; ++i)
    {
        VoiceData real {}, ref {};
        for (auto& n : real) n = (std::uint8_t) (rng() & 0x0F);
        for (auto& n : ref)  n = (std::uint8_t) (rng() & 0x0F);
        for (const auto& p : allParams())
        {
            if (p.kind == ParamKind::Knob)
            {
                setParam (real, p, (int) (rng() % 255));
                setParam (ref,  p, (int) (rng() % 255));
            }
            else
            {
                setParam (real, p, p.choices[rng() % p.choices.size()].value);
                setParam (ref,  p, p.choices[rng() % p.choices.size()].value);
            }
        }
        const auto dump = patchDumpOf (real);
        const auto r = voiceFromCcDump (ref, dump);
        CHECK (compareWithCcDump (r.voice, dump).empty());
        for (const auto& p : allParams())
            if (p.kind != ParamKind::Knob && p.coding != CcCoding::ArpNibble)
                CHECK (getParam (r.voice, p) == getParam (real, p));
        CHECK ((r.voice[62] & 0x0F) == (real[62] & 0x0F));
        for (auto n : r.voice)
            CHECK (n <= 0x0F);
    }

    // CCs missing from the dump keep the reference; impossible values are reported, not applied
    {
        const auto& ref = bank.voices[5];
        std::vector<int> dump (128, -1);
        dump[30] = 10; // filter frequency only
        auto r = voiceFromCcDump (ref, dump);
        CHECK (getParam (r.voice, *findParam ("filterFreq")) / 2 == 10);
        for (const auto& p : allParams())
            if (std::string (p.id) != "filterFreq")
                CHECK (getParam (r.voice, p) == getParam (ref, p));

        std::vector<int> bad (128, -1);
        bad[50] = 127; // not a value the firmware ever sends for Function
        r = voiceFromCcDump (ref, bad);
        CHECK (r.voice == ref);
        CHECK (r.unreadable.size() == 1);
    }
}

static void testKnownFactoryValues()
{
    const auto bank = factoryBank();
    // voice -1 (GlassandoLead): first two wire bytes 00 0B -> filter drive 0xB0
    CHECK (getParam (bank.voices[0], *findParam ("filterDrive")) == 176);
    CHECK (std::string (factory::kVoiceNames[0]) == "GlassandoLead");
    CHECK (std::string (factory::kVoiceNames[11]) == "Hollow Bass");
    CHECK (std::string (factory::kVoiceNames[12]) == "PWMLead");
    CHECK (std::string (factory::kVoiceNames[35]) == "Snare");

    // no knob anywhere in the factory bank exceeds 254
    for (const auto& v : bank.voices)
        for (const auto& p : allParams())
            if (p.kind == ParamKind::Knob)
                CHECK (getParam (v, p) <= kKnobMaxEditable);
}

static void testHarmonics()
{
    WaveBank w {};
    for (int t = 0; t < kNumWaveTables; ++t)
        for (int h = 0; h < kNumHarmonics; ++h)
            w.setHarmonic (t, h, (t * 37 + h * 11) & 0xFF);
    for (int t = 0; t < kNumWaveTables; ++t)
        for (int h = 0; h < kNumHarmonics; ++h)
            CHECK (w.getHarmonic (t, h) == ((t * 37 + h * 11) & 0xFF));
    for (auto b : w.raw)
        CHECK (b <= 0x0F);

    DumpContents d, back;
    d.waves = w;
    std::string err;
    CHECK (parseStream (buildFile (d), back, err));
    CHECK (back.waves && *back.waves == w);
    CHECK (back.warnings.empty());
    CHECK (back.suspectBlocks.empty());
}

static void testRejections()
{
    const auto good = buildVoiceMessage (factoryBank());
    DumpContents d;
    std::string err;

    CHECK (parseMessage (good.data(), good.size(), d, err));

    auto expectFail = [] (std::vector<std::uint8_t> m) {
        DumpContents out;
        std::string e;
        out.warnings.push_back ("sentinel");
        const bool ok = parseStream (m, out, e);
        CHECK (! ok);
        CHECK (! e.empty());
        CHECK (out.warnings.size() == 1 && out.warnings[0] == "sentinel"); // untouched on failure
        return ok;
    };

    { auto m = good; m[1] = 0x43;            expectFail (m); }   // other manufacturer
    { auto m = good; m[2] = 0x07;            expectFail (m); }   // unknown type
    { auto m = good; m[100] = 0x10;          expectFail (m); }   // not a nibble
    { auto m = good; m[100] = 0x90;          expectFail (m); }   // status byte inside
    { auto m = good; m.pop_back();           expectFail (m); }   // truncated
    { auto m = good; m.erase (m.begin() + 10); expectFail (m); } // short voice block
    { auto m = good; m.insert (m.begin() + 10, 0x01); expectFail (m); } // long voice block
    { std::vector<std::uint8_t> m { 0xF0, 0x2B, 0xF7 };   expectFail (m); }
    { std::vector<std::uint8_t> m;                         expectFail (m); }
    { auto m = good; m.insert (m.begin(), 0x00); expectFail (m); } // garbage before
    { auto m = good; m.push_back (0x90);     expectFail (m); }   // garbage after
    { auto m = good; m.insert (m.end(), good.begin(), good.end()); expectFail (m); } // two voice banks
}

static void testCombinedMessage()
{
    // A single F0..F7 holding waveform + voice + sequence blocks (STORE dump style).
    WaveBank w {};
    w.setHarmonic (0, 0, 200);
    const auto voices = buildVoiceMessage (factoryBank());
    std::vector<std::uint8_t> m { 0xF0, 0x2B, 0x01 };
    m.insert (m.end(), w.raw.begin(), w.raw.end());
    m.insert (m.end(), voices.begin() + 2, voices.end() - 1); // type 2 + data
    m.push_back (0x03);
    for (int i = 0; i < 50; ++i) m.push_back ((std::uint8_t) (i & 0x0F));
    m.push_back (0xF7);

    DumpContents d;
    std::string err;
    CHECK (parseStream (m, d, err));
    CHECK (d.waves && *d.waves == w);
    CHECK (d.voices && *d.voices == factoryBank());
    CHECK (d.sequences && d.sequences->size() == 50);
}

static void testFuzz()
{
    std::mt19937 rng (1234);
    for (int iter = 0; iter < 20000; ++iter)
    {
        std::vector<std::uint8_t> m;
        const int len = (int) (rng() % 3000);
        if (rng() % 2) { m.push_back (0xF0); m.push_back (0x2B); m.push_back ((std::uint8_t) (rng() % 4)); }
        for (int i = 0; i < len; ++i)
            m.push_back ((std::uint8_t) (rng() % 5 == 0 ? rng() & 0xFF : rng() & 0x0F));
        if (rng() % 2) m.push_back (0xF7);
        DumpContents d;
        std::string err;
        if (parseStream (m, d, err))
        {
            // anything accepted must rebuild to the same bytes (single message case)
            const auto again = buildFile (d);
            DumpContents d2;
            CHECK (parseStream (again, d2, err));
            CHECK (d2.voices == d.voices && d2.waves == d.waves && d2.sequences == d.sequences);
        }
    }

    // random valid banks round-trip exactly
    for (int iter = 0; iter < 200; ++iter)
    {
        VoiceBank b;
        for (auto& v : b.voices)
            for (auto& x : v)
                x = (std::uint8_t) (rng() & 0x0F);
        DumpContents d;
        std::string err;
        const auto msg = buildVoiceMessage (b);
        CHECK (parseStream (msg, d, err));
        CHECK (d.voices && *d.voices == b);
    }
}

static void testInitVoice()
{
    const auto v = initVoice();
    const auto get = [&v] (const char* id) { return getParam (v, *findParam (id)); };
    for (const auto& p : allParams())
    {
        const int x = getParam (v, p);
        if (p.kind == ParamKind::Knob)
            CHECK (x >= 1 && x <= kKnobMaxEditable);
        else
            CHECK (isValidChoice (p, x));
        // Live CC gives the OSCar exactly this value
        if (p.coding != CcCoding::None && p.coding != CcCoding::ArpNibble)
            CHECK (firmwareValueFromCc (p.coding, ccValueFor (p.coding, x)) == x);
    }
    CHECK (get ("wave1") == 1 && get ("oscBalance") == 1);                 // oscillator 1 alone, sawtooth
    CHECK (get ("filterFreq") == 254 && get ("resonance") == 1 && get ("envFilter") == 128);
    CHECK (get ("volAttack") == 1 && get ("volSustain") == 254);
    CHECK (get ("lfoPitch") == 128 && get ("lfoFilter") == 128 && get ("glideRate") == 1);
    CHECK (choiceLabel (*findParam ("function"), get ("function")) == "Normal");
    CHECK (choiceLabel (*findParam ("trigger"), get ("trigger")) == "Multi");
    CHECK (choiceLabel (*findParam ("pwmRate"), get ("pwmRate")) == "Normal");
    CHECK (choiceLabel (*findParam ("tuneSemi"), get ("tuneSemi")) == "0");
    CHECK (choiceLabel (*findParam ("osc2Semi"), get ("osc2Semi")) == "0");
    for (auto b : v)
        CHECK (b <= 0x0F);   // nibbles only
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: CoreTests <factory_codemechanic.syx>\n");
        return 2;
    }

    testFactoryFile (argv[1]);
    testSlots();
    testInitVoice();
    testParamMapLayout();
    testEditIsolation();
    testCcNumbers();
    testCcCoding();
    testPwmRate();
    testDumpComparison();
    testRecallFromDump();
    testKnownFactoryValues();
    testHarmonics();
    testRejections();
    testCombinedMessage();
    testFuzz();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
