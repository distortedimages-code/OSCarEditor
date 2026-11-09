#include "Generator.h"

#include "FactoryBank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oscar
{
namespace
{
    const ParamDef& P (const char* id)
    {
        const auto* p = findParam (id);
        if (p == nullptr)
        {
            std::fprintf (stderr, "Generator: unknown parameter %s\n", id);
            std::abort();
        }
        return *p;
    }

    struct SectionEntry { const char* id; GenSection section; };

    // Everything the generator may change. Anything else is never touched.
    const SectionEntry kSections[] = {
        { "wave1", GenSection::Oscillators }, { "wave2", GenSection::Oscillators },
        { "octaveShift", GenSection::Oscillators }, { "osc2Semi", GenSection::Oscillators },
        { "oscBalance", GenSection::Oscillators }, { "noiseBalance", GenSection::Oscillators },
        { "pulseWidth", GenSection::Oscillators }, { "detune", GenSection::Oscillators },

        { "filterType", GenSection::Filter }, { "filterFreq", GenSection::Filter }, { "resonance", GenSection::Filter },
        { "filterSep", GenSection::Filter }, { "filterDrive", GenSection::Filter }, { "envFilter", GenSection::Filter },

        { "filtAttack", GenSection::FilterEnv }, { "filtDecay", GenSection::FilterEnv },
        { "filtSustain", GenSection::FilterEnv }, { "filtRelease", GenSection::FilterEnv },

        { "volAttack", GenSection::AmpEnv }, { "volDecay", GenSection::AmpEnv },
        { "volSustain", GenSection::AmpEnv }, { "volRelease", GenSection::AmpEnv },

        { "lfoWave", GenSection::Modulation }, { "lfoRate", GenSection::Modulation }, { "introDelay", GenSection::Modulation },
        { "lfoPitch", GenSection::Modulation }, { "lfoFilter", GenSection::Modulation },

        { "trigger", GenSection::Performance }, { "glideType", GenSection::Performance }, { "glideRate", GenSection::Performance },

        { "lfoToOsc1", GenSection::Tauntek }, { "lfoSync", GenSection::Tauntek }, { "pwmRate", GenSection::Tauntek },
    };

    using Weights = std::vector<std::pair<int, double>>;

    struct KnobProfile
    {
        const char* id;
        int lo, hi, mode;
        double restChance = 0.0;   // chance of using restValue instead of the range
        int restValue = 0;
    };

    struct SwitchProfile
    {
        const char* id;
        Weights weights;
    };

    struct Profile
    {
        bool wild;
        std::vector<KnobProfile> knobs;
        std::vector<SwitchProfile> switches;
    };

    Weights allValues (const char* id, int skip = -1)
    {
        Weights w;
        for (const auto& c : P (id).choices)
            if (c.value != skip)
                w.emplace_back (c.value, 1.0);
        return w;
    }

    const Weights kMusicalIntervals { { 7, 0.78 }, { 0, 0.10 }, { 2, 0.04 }, { 3, 0.02 }, { 4, 0.01 }, { 14, 0.03 }, { 12, 0.02 } };

    const Weights kLeadWaves { { 1, .25 }, { 2, .20 }, { 3, .10 }, { 4, .20 }, { 0, .05 }, { 6, .03 }, { 7, .03 }, { 8, .03 },
                               { 9, .03 }, { 10, .03 }, { 11, .01 }, { 12, .01 }, { 13, .01 }, { 14, .01 }, { 15, .01 } };

    // Profiles. Ranges come from the factory bank (1 = fastest attack / closed, 254 = slowest / fully open),
    // with typical values close to what factory voices of that kind use.
    std::vector<Profile> makeProfiles()
    {
        std::vector<Profile> p ((size_t) kNumFamilies);

        p[(size_t) Family::Bass] = { false,
            { { "oscBalance", 110, 150, 128 }, { "noiseBalance", 0, 1, 1 }, { "pulseWidth", 100, 254, 190 }, { "detune", 130, 175, 152 },
              { "filterFreq", 30, 150, 90 }, { "resonance", 1, 100, 40 }, { "filterSep", 1, 150, 40 }, { "filterDrive", 190, 254, 240 },
              { "envFilter", 150, 240, 200 },
              { "filtAttack", 1, 8, 1 }, { "filtDecay", 50, 170, 100 }, { "filtSustain", 1, 130, 60 }, { "filtRelease", 1, 130, 80 },
              { "volAttack", 1, 6, 1 }, { "volDecay", 70, 200, 120 }, { "volSustain", 140, 254, 230 }, { "volRelease", 1, 110, 60 },
              { "lfoRate", 150, 223, 192 }, { "introDelay", 1, 150, 100 }, { "lfoPitch", 125, 150, 135 }, { "lfoFilter", 115, 145, 128 },
              { "glideRate", 20, 60, 35, 0.8, 1 } },
            { { "wave1", { { 1, .30 }, { 2, .20 }, { 3, .15 }, { 4, .25 }, { 0, .10 } } },
              { "wave2", { { 1, .30 }, { 2, .20 }, { 3, .15 }, { 4, .25 }, { 0, .05 }, { 7, .05 } } },
              { "osc2Semi", { { 7, .85 }, { 0, .10 }, { 14, .05 } } },
              { "filterType", { { 0, .60 }, { 4, .30 }, { 1, .10 } } },
              { "trigger", { { 1, .70 }, { 0, .30 } } }, { "glideType", { { 0, .80 }, { 4, .20 } } },
              { "lfoWave", { { 0, .80 }, { 1, .10 }, { 3, .10 } } },
              { "lfoToOsc1", { { 0, 1.0 } } }, { "lfoSync", { { 0, .90 }, { 1, .10 } } },
              { "pwmRate", { { 2, .70 }, { 1, .15 }, { 3, .15 } } } } };

        p[(size_t) Family::Lead] = { false,
            { { "oscBalance", 110, 180, 128 }, { "noiseBalance", 0, 1, 1 }, { "pulseWidth", 60, 254, 200 }, { "detune", 130, 185, 150 },
              { "filterFreq", 110, 230, 160 }, { "resonance", 1, 120, 45 }, { "filterSep", 1, 160, 60 }, { "filterDrive", 176, 254, 227 },
              { "envFilter", 110, 210, 140 },
              { "filtAttack", 1, 40, 1 }, { "filtDecay", 70, 220, 140 }, { "filtSustain", 40, 254, 150 }, { "filtRelease", 70, 180, 120 },
              { "volAttack", 1, 60, 1 }, { "volDecay", 100, 254, 150 }, { "volSustain", 180, 254, 254 }, { "volRelease", 70, 180, 130 },
              { "lfoRate", 150, 223, 192 }, { "introDelay", 1, 160, 128 }, { "lfoPitch", 125, 160, 148 }, { "lfoFilter", 115, 145, 128 },
              { "glideRate", 20, 110, 60, 0.7, 1 } },
            { { "wave1", kLeadWaves }, { "wave2", kLeadWaves },
              { "osc2Semi", { { 7, .80 }, { 0, .12 }, { 2, .05 }, { 12, .03 } } },
              { "filterType", { { 0, .55 }, { 1, .20 }, { 4, .20 }, { 5, .05 } } },
              { "trigger", { { 1, .60 }, { 0, .40 } } }, { "glideType", { { 0, .60 }, { 4, .40 } } },
              { "lfoWave", { { 0, .85 }, { 1, .05 }, { 2, .05 }, { 3, .05 } } },
              { "lfoToOsc1", { { 0, .95 }, { 1, .05 } } }, { "lfoSync", { { 0, .85 }, { 1, .15 } } },
              { "pwmRate", { { 2, .70 }, { 1, .15 }, { 3, .15 } } } } };

        p[(size_t) Family::Pad] = { false,
            { { "oscBalance", 115, 150, 128 }, { "noiseBalance", 0, 40, 1 }, { "pulseWidth", 120, 254, 200 }, { "detune", 150, 190, 168 },
              { "filterFreq", 100, 200, 150 }, { "resonance", 1, 80, 30 }, { "filterSep", 40, 200, 110 }, { "filterDrive", 176, 254, 220 },
              { "envFilter", 115, 175, 140 },
              { "filtAttack", 40, 170, 100 }, { "filtDecay", 140, 254, 200 }, { "filtSustain", 100, 254, 180 }, { "filtRelease", 130, 200, 170 },
              { "volAttack", 50, 170, 110 }, { "volDecay", 150, 254, 220 }, { "volSustain", 200, 254, 254 }, { "volRelease", 130, 200, 175 },
              { "lfoRate", 90, 190, 150 }, { "introDelay", 60, 160, 128 }, { "lfoPitch", 125, 150, 138 }, { "lfoFilter", 110, 170, 140 },
              { "glideRate", 1, 30, 1, 0.9, 1 } },
            { { "wave1", { { 4, .30 }, { 1, .20 }, { 0, .10 }, { 2, .05 }, { 6, .05 }, { 7, .05 }, { 8, .05 }, { 9, .05 }, { 10, .05 },
                           { 11, .02 }, { 12, .02 }, { 13, .02 }, { 14, .02 }, { 15, .02 } } },
              { "wave2", { { 4, .30 }, { 1, .20 }, { 0, .10 }, { 2, .05 }, { 6, .05 }, { 7, .05 }, { 8, .05 }, { 9, .05 }, { 10, .05 },
                           { 11, .02 }, { 12, .02 }, { 13, .02 }, { 14, .02 }, { 15, .02 } } },
              { "osc2Semi", { { 7, .75 }, { 0, .15 }, { 2, .05 }, { 14, .05 } } },
              { "filterType", { { 0, .40 }, { 4, .30 }, { 1, .20 }, { 5, .10 } } },
              { "trigger", { { 0, .50 }, { 1, .50 } } }, { "glideType", { { 0, .90 }, { 4, .10 } } },
              { "lfoWave", { { 0, .70 }, { 1, .10 }, { 2, .10 }, { 3, .10 } } },
              { "lfoToOsc1", { { 0, .90 }, { 1, .10 } } }, { "lfoSync", { { 0, .90 }, { 1, .10 } } },
              { "pwmRate", { { 0, .30 }, { 1, .40 }, { 2, .30 } } } } };

        p[(size_t) Family::Percussive] = { false,
            { { "oscBalance", 110, 170, 130 }, { "noiseBalance", 120, 254, 220, 0.5, 1 }, { "pulseWidth", 80, 254, 180 },
              { "detune", 130, 180, 152 },
              { "filterFreq", 20, 160, 80 }, { "resonance", 1, 120, 45 }, { "filterSep", 1, 200, 60 }, { "filterDrive", 176, 254, 240 },
              { "envFilter", 170, 254, 220 },
              { "filtAttack", 1, 4, 1 }, { "filtDecay", 30, 150, 80 }, { "filtSustain", 1, 60, 10 }, { "filtRelease", 1, 140, 60 },
              { "volAttack", 1, 4, 1 }, { "volDecay", 40, 160, 100 }, { "volSustain", 1, 30, 1 }, { "volRelease", 1, 140, 60 },
              { "lfoRate", 150, 223, 192 }, { "introDelay", 1, 150, 100 }, { "lfoPitch", 125, 150, 135 }, { "lfoFilter", 115, 145, 128 },
              { "glideRate", 1, 1, 1 } },
            { { "wave1", { { 1, .20 }, { 2, .20 }, { 3, .20 }, { 4, .20 }, { 0, .10 }, { 8, .05 }, { 9, .05 } } },
              { "wave2", { { 1, .20 }, { 2, .20 }, { 3, .20 }, { 4, .20 }, { 0, .10 }, { 8, .05 }, { 9, .05 } } },
              { "osc2Semi", { { 7, .75 }, { 0, .10 }, { 2, .05 }, { 14, .05 }, { 12, .05 } } },
              { "filterType", { { 0, .60 }, { 4, .25 }, { 1, .15 } } },
              { "trigger", { { 1, .90 }, { 0, .10 } } }, { "glideType", { { 0, 1.0 } } },
              { "lfoWave", { { 0, .90 }, { 3, .10 } } },
              { "lfoToOsc1", { { 0, 1.0 } } }, { "lfoSync", { { 0, 1.0 } } }, { "pwmRate", { { 2, .70 }, { 3, .30 } } } } };

        p[(size_t) Family::Texture] = { true,
            { { "oscBalance", 60, 230, 128 }, { "noiseBalance", 100, 254, 200, 0.4, 1 }, { "pulseWidth", 0, 254, 128 }, { "detune", 0, 254, 160 },
              { "filterFreq", 1, 254, 130 }, { "resonance", 40, 200, 120 }, { "filterSep", 1, 254, 128 }, { "filterDrive", 128, 254, 220 },
              { "envFilter", 105, 254, 180 },
              { "filtAttack", 1, 200, 40 }, { "filtDecay", 1, 254, 150 }, { "filtSustain", 1, 254, 128 }, { "filtRelease", 1, 220, 140 },
              { "volAttack", 1, 170, 40 }, { "volDecay", 60, 254, 180 }, { "volSustain", 120, 254, 220 }, { "volRelease", 60, 220, 160 },
              { "lfoRate", 0, 254, 160 }, { "introDelay", 1, 254, 100 }, { "lfoPitch", 30, 254, 150 }, { "lfoFilter", 0, 254, 140 },
              { "glideRate", 1, 150, 60, 0.5, 1 } },
            { { "wave1", allValues ("wave1", 5) }, { "wave2", allValues ("wave2") },
              { "octaveShift", { { 1, .3 }, { 2, .4 }, { 3, .3 } } },
              { "osc2Semi", allValues ("osc2Semi") },
              { "filterType", allValues ("filterType") },
              { "trigger", { { 0, .25 }, { 1, .25 }, { 2, .2 }, { 3, .15 }, { 4, .15 } } },
              { "glideType", allValues ("glideType") },
              { "lfoWave", allValues ("lfoWave") },
              { "lfoToOsc1", { { 0, .60 }, { 1, .40 } } }, { "lfoSync", { { 0, .60 }, { 1, .40 } } },
              { "pwmRate", allValues ("pwmRate") } } };

        return p;
    }

    const std::vector<Profile>& profiles()
    {
        static const std::vector<Profile> p = makeProfiles();
        return p;
    }

    bool isLowPass (int filterType)  { return filterType == 0 || filterType == 4; }
    bool isHighPass (int filterType) { return filterType == 2 || filterType == 6; }

    bool unlocked (const GenLocks& l, GenSection s) { return ! l.isLocked (s); }

    int get (const VoiceData& v, const char* id) { return getParam (v, P (id)); }

    void set (VoiceData& v, const char* id, int value)
    {
        const auto& p = P (id);
        setParam (v, p, std::clamp (value, 0, maxParamValue (p)));
    }
}

//==============================================================================
const char* sectionName (GenSection s)
{
    switch (s)
    {
        case GenSection::Oscillators: return "Oscillators";
        case GenSection::Filter:      return "Filter";
        case GenSection::FilterEnv:   return "Filter Env";
        case GenSection::AmpEnv:      return "Amp Env";
        case GenSection::Modulation:  return "Modulation";
        case GenSection::Performance: return "Trigger & Glide";
        case GenSection::Tauntek:     return "Tauntek";
        case GenSection::Count:       break;
    }
    return "";
}

const char* familyName (Family f)
{
    switch (f)
    {
        case Family::Bass:       return "Bass";
        case Family::Lead:       return "Lead";
        case Family::Pad:        return "Pad";
        case Family::Percussive: return "Percussive";
        case Family::Texture:    return "Texture / Noise";
        case Family::Count:      break;
    }
    return "";
}

bool generatorSection (const ParamDef& p, GenSection& out)
{
    for (const auto& e : kSections)
        if (std::strcmp (e.id, p.id) == 0)
        {
            out = e.section;
            return true;
        }
    return false;
}

Corpus Corpus::withFactory (const std::vector<VoiceData>& extra)
{
    Corpus c;
    for (int i = 0; i < kNumVoices; ++i)
    {
        VoiceData v;
        std::copy (factory::kVoiceBank + i * kVoiceSize, factory::kVoiceBank + (i + 1) * kVoiceSize, v.begin());
        c.voices.push_back (v);
    }
    c.voices.insert (c.voices.end(), extra.begin(), extra.end());
    return c;
}

Envelope Envelope::fromCorpus (const Corpus& corpus)
{
    const auto& c = corpus.voices.empty() ? Corpus::withFactory().voices : corpus.voices;
    Envelope e;
    e.lo.fill (0);
    e.hi.fill (kKnobMaxEditable);
    for (const auto& p : allParams())
    {
        if (p.kind != ParamKind::Knob)
            continue;
        int lo = 255, hi = -1;
        for (const auto& v : c)
        {
            const int x = std::min (getParam (v, p), kKnobMaxEditable);
            lo = std::min (lo, x);
            hi = std::max (hi, x);
        }
        e.lo[(size_t) p.offset] = std::max (0, lo - 8);
        e.hi[(size_t) p.offset] = std::min (kKnobMaxEditable, hi + 8);
    }
    return e;
}

int Envelope::clamp (const ParamDef& p, int value) const
{
    if (p.kind != ParamKind::Knob)
        return value;
    return std::clamp (value, lo[(size_t) p.offset], hi[(size_t) p.offset]);
}

//==============================================================================
Generator::Generator (std::uint32_t seed) : rng (seed) {}

double Generator::uniform()
{
    return std::uniform_real_distribution<double> (0.0, 1.0) (rng);
}

double Generator::gaussian()
{
    return std::normal_distribution<double> (0.0, 1.0) (rng);
}

int Generator::triangular (int lo, int hi, int mode)
{
    if (hi <= lo)
        return lo;
    mode = std::clamp (mode, lo, hi);
    const double a = lo, b = hi, c = mode, u = uniform();
    const double f = (c - a) / (b - a);
    const double x = u < f ? a + std::sqrt (u * (b - a) * (c - a))
                           : b - std::sqrt ((1.0 - u) * (b - a) * (b - c));
    return std::clamp ((int) std::lround (x), lo, hi);
}

int Generator::weighted (const Weights& choices)
{
    double total = 0;
    for (const auto& c : choices)
        total += c.second;
    double r = uniform() * total;
    for (const auto& c : choices)
    {
        r -= c.second;
        if (r <= 0)
            return c.first;
    }
    return choices.back().first;
}

//==============================================================================
namespace
{
    bool musicalInterval (int s, const VoiceData* base)
    {
        // unison, fifth and fourth up or down, thirds up (factory voice -2 uses a major third)
        if (s == 7 || s == 0 || s == 2 || s == 3 || s == 4 || s == 12 || s == 14)
            return true;
        return base != nullptr && s == getParam (*base, P ("osc2Semi"));
    }
}

std::vector<std::string> Generator::ruleViolations (const VoiceData& v, const GenLocks& locks, bool wild,
                                                    const Envelope* learned, const VoiceData* base)
{
    std::vector<std::string> out;

    if (unlocked (locks, GenSection::AmpEnv))
    {
        if (get (v, "volSustain") < 100 && get (v, "volDecay") < 40)  out.push_back ("amp: silent (low sustain and very short decay)");
        if (get (v, "volAttack") > 200)                                out.push_back ("amp: attack too slow");
        if (get (v, "volRelease") > 220)                               out.push_back ("amp: release too long");
    }

    if (unlocked (locks, GenSection::Filter))
    {
        const int type = get (v, "filterType"), freq = get (v, "filterFreq");
        const bool envOpens = get (v, "envFilter") >= 170 && (get (v, "filtDecay") >= 40 || get (v, "filtSustain") >= 60);
        if (isLowPass (type) && freq < 60 && ! envOpens)   out.push_back ("filter: low-pass closed");
        if (isHighPass (type) && freq > 190)               out.push_back ("filter: high-pass too thin");
        if (get (v, "resonance") > (wild ? 210 : 150))     out.push_back ("filter: resonance too high");
        if (get (v, "filterDrive") < (wild ? 100 : 128))   out.push_back ("filter: drive too low");
    }

    if (unlocked (locks, GenSection::Oscillators))
    {
        if (get (v, "wave1") == 5 && get (v, "noiseBalance") < 150)  out.push_back ("osc: oscillator 1 off");
        const int bal = get (v, "oscBalance");
        if (bal < (wild ? 60 : 100) || bal > (wild ? 240 : 235))     out.push_back ("osc: balance extreme");
        if (! wild && ! musicalInterval (get (v, "osc2Semi"), base))
            out.push_back ("osc: interval not musical");
    }

    if (! wild && learned != nullptr)
    {
        for (const auto& p : allParams())
        {
            GenSection s;
            if (p.kind == ParamKind::Knob && generatorSection (p, s) && unlocked (locks, s)
                && learned->clamp (p, getParam (v, p)) != getParam (v, p))
            {
                out.push_back (std::string ("outside learned range: ") + p.id);
            }
        }
    }
    return out;
}

void Generator::enforceRules (VoiceData& v, const GenLocks& locks, bool wild, const Envelope& env, const VoiceData* base)
{
    // 1. learned limits
    if (! wild)
        for (const auto& p : allParams())
        {
            GenSection s;
            if (p.kind == ParamKind::Knob && generatorSection (p, s) && unlocked (locks, s))
                setParam (v, p, env.clamp (p, getParam (v, p)));
        }

    // 2. audibility
    if (unlocked (locks, GenSection::AmpEnv))
    {
        if (get (v, "volSustain") < 100 && get (v, "volDecay") < 40)
            set (v, "volDecay", 40 + (int) (uniform() * 60));
        if (get (v, "volAttack") > 200)  set (v, "volAttack", 200);
        if (get (v, "volRelease") > 220) set (v, "volRelease", 220);
    }

    if (unlocked (locks, GenSection::Filter))
    {
        const int type = get (v, "filterType");
        const bool envOpens = get (v, "envFilter") >= 170 && (get (v, "filtDecay") >= 40 || get (v, "filtSustain") >= 60);
        if (isLowPass (type) && get (v, "filterFreq") < 60 && ! envOpens)
            set (v, "filterFreq", 60 + (int) (uniform() * 40));
        if (isHighPass (type) && get (v, "filterFreq") > 190)
            set (v, "filterFreq", 120 + (int) (uniform() * 70));
        const int qMax = wild ? 210 : 150;
        if (get (v, "resonance") > qMax)
            set (v, "resonance", qMax);
        const int driveMin = wild ? 100 : 128;
        if (get (v, "filterDrive") < driveMin)
            set (v, "filterDrive", driveMin);
    }

    if (unlocked (locks, GenSection::Oscillators))
    {
        if (get (v, "wave1") == 5 && get (v, "noiseBalance") < 150)
            set (v, "wave1", 1);
        set (v, "oscBalance", std::clamp (get (v, "oscBalance"), wild ? 60 : 100, wild ? 240 : 235));
        if (! wild && ! musicalInterval (get (v, "osc2Semi"), base))
            set (v, "osc2Semi", 7);
    }

    // 3. learned limits again (the fixes above stay inside them, this is a safety net)
    if (! wild)
        for (const auto& p : allParams())
        {
            GenSection s;
            if (p.kind == ParamKind::Knob && generatorSection (p, s) && unlocked (locks, s))
                setParam (v, p, env.clamp (p, getParam (v, p)));
        }
}

//==============================================================================
VoiceData Generator::vary (const VoiceData& base, double amount, const GenLocks& locks, const Corpus& corpus)
{
    amount = std::clamp (amount, 0.0, 1.0);
    if (amount <= 0.0)
        return base;

    const bool wild = amount > 0.75;
    const auto env = Envelope::fromCorpus (corpus);
    auto v = base;

    const double changeChance = 0.25 + 0.75 * amount;
    const double sigma = 4.0 + amount * 70.0;

    for (const auto& p : allParams())
    {
        GenSection s;
        if (! generatorSection (p, s) || locks.isLocked (s))
            continue;

        const int cur = getParam (base, p);

        if (p.kind == ParamKind::Knob)
        {
            if (uniform() < changeChance)
                setParam (v, p, std::clamp (cur + (int) std::lround (gaussian() * sigma), 0, kKnobMaxEditable));
            continue;
        }

        if (uniform() >= amount * 0.3)
            continue;

        const std::string id = p.id;
        Weights w;
        if (id == "wave1")            w = wild ? allValues ("wave1", 5) : kLeadWaves;
        else if (id == "wave2")       w = wild ? allValues ("wave2") : kLeadWaves;
        else if (id == "osc2Semi")    w = wild ? allValues ("osc2Semi") : kMusicalIntervals;
        else if (id == "filterType")  w = wild ? allValues ("filterType") : Weights { { 0, 1 }, { 1, 1 }, { 2, .5 }, { 4, 1 }, { 5, .5 } };
        else if (id == "trigger")     w = wild ? Weights { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 } } : Weights { { 0, 1 }, { 1, 1 } };
        else if (id == "glideType")   w = wild ? allValues ("glideType") : Weights { { 0, 1 }, { 4, 1 } };
        else if (id == "lfoWave")     w = wild ? allValues ("lfoWave") : Weights { { 0, 3 }, { 1, 1 }, { 2, 1 }, { 3, 1 } };
        else if (id == "octaveShift") { if (wild) w = Weights { { 1, 1 }, { 2, 1 }, { 3, 1 } }; }
        else if (id == "pwmRate")     w = allValues ("pwmRate");
        else if (id == "lfoToOsc1" || id == "lfoSync") { if (wild) w = Weights { { 0, 1 }, { 1, 1 } }; }

        if (! w.empty())
            setParam (v, p, weighted (w));
    }

    enforceRules (v, locks, wild, env, &base);
    return v;
}

VoiceData Generator::create (const VoiceData& base, Family family, const GenLocks& locks, const Corpus& corpus)
{
    const auto& prof = profiles()[(size_t) family];
    const auto env = Envelope::fromCorpus (corpus);
    const auto& voices = corpus.voices.empty() ? Corpus::withFactory().voices : corpus.voices;
    auto v = base;

    for (int si = 0; si < kNumGenSections; ++si)
    {
        const auto section = (GenSection) si;
        if (locks.isLocked (section))
            continue;

        std::vector<const KnobProfile*> knobs;
        for (const auto& k : prof.knobs)
        {
            GenSection s;
            if (generatorSection (P (k.id), s) && s == section)
                knobs.push_back (&k);
        }

        // Borrow the whole section from a real voice that fits the family,
        // so values that belong together (an envelope shape...) stay coherent.
        const VoiceData* donor = nullptr;
        if (! knobs.empty() && uniform() < 0.6)
        {
            std::vector<std::pair<const VoiceData*, double>> fits;
            for (const auto& cv : voices)
            {
                int inside = 0;
                for (const auto* k : knobs)
                {
                    const int x = getParam (cv, P (k->id));
                    if (x >= k->lo && x <= k->hi) ++inside;
                }
                const double score = (double) inside / (double) knobs.size();
                if (score >= 0.75)
                    fits.emplace_back (&cv, score);
            }
            if (! fits.empty())
            {
                double total = 0;
                for (const auto& f : fits) total += f.second;
                double r = uniform() * total;
                for (const auto& f : fits)
                {
                    r -= f.second;
                    if (r <= 0) { donor = f.first; break; }
                }
                if (donor == nullptr) donor = fits.back().first;
            }
        }

        for (const auto* k : knobs)
        {
            int value;
            if (k->restChance > 0 && uniform() < k->restChance)
            {
                value = k->restValue;
            }
            else
            {
                const int d = donor != nullptr ? getParam (*donor, P (k->id)) : -1;
                if (d >= k->lo && d <= k->hi)
                    value = d + (int) std::lround (gaussian() * ((k->hi - k->lo) * 0.06 + 2.0));
                else
                    value = triangular (k->lo, k->hi, k->mode);
                value = std::clamp (value, k->lo, k->hi);
            }
            set (v, k->id, value);
        }

        for (const auto& sp : prof.switches)
        {
            GenSection s;
            if (generatorSection (P (sp.id), s) && s == section && ! sp.weights.empty())
                set (v, sp.id, weighted (sp.weights));
        }
    }

    enforceRules (v, locks, prof.wild, env, &base);
    return v;
}

VoiceData Generator::breed (const VoiceData& a, const VoiceData& b, double mutation, const GenLocks& locks, const Corpus& corpus)
{
    auto v = a;
    for (int si = 0; si < kNumGenSections; ++si)
    {
        const auto section = (GenSection) si;
        if (locks.isLocked (section) || uniform() < 0.5)
            continue;
        for (const auto& p : allParams())
        {
            GenSection s;
            if (generatorSection (p, s) && s == section)
                setParam (v, p, getParam (b, p));
        }
    }

    mutation = std::clamp (mutation, 0.0, 1.0);
    if (mutation > 0.0)
        return vary (v, mutation * 0.5, locks, corpus);

    const auto mixed = v;   // either parent's interval is fine
    enforceRules (v, locks, false, Envelope::fromCorpus (corpus), &mixed);
    return v;
}

} // namespace oscar
