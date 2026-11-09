// Tests for the sound generator: every result must be a valid OSCar voice that
// respects locks, never-touched parameters and the audibility rules.

#include "../Source/core/FactoryBank.h"
#include "../Source/core/Generator.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <set>
#include <string>

using namespace oscar;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (! (cond)) { ++failures; if (failures < 40) std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static VoiceData factoryVoice (int i)
{
    VoiceData v;
    for (int k = 0; k < kVoiceSize; ++k)
        v[(size_t) k] = factory::kVoiceBank[i * kVoiceSize + k];
    return v;
}

static const char* const kNeverTouched[] = { "tuneSemi", "gateTime", "arpDir", "arpMode", "octaveSet",
                                             "bendAmount", "mwPitch", "mwFilter", "function" };

// Every value is a valid setting, or the base's own value (factory voice 35 stores octave 1,
// which is outside the documented range: the generator must leave it alone, not "fix" it).
static bool isValidVoice (const VoiceData& v, const VoiceData& base)
{
    for (auto n : v)
        if (n > 0x0F)
            return false;
    for (const auto& p : allParams())
    {
        const int x = getParam (v, p);
        if (x == getParam (base, p))
            continue;
        if (p.kind == ParamKind::Knob ? (x < 0 || x > kKnobMaxEditable) : ! isValidChoice (p, x))
            return false;
    }
    return true;
}

// Locked sections, never-touched parameters and unowned bits are exactly as in 'base'.
static bool respectsLocks (const VoiceData& out, const VoiceData& base, const GenLocks& locks)
{
    for (const auto& p : allParams())
    {
        GenSection s;
        const bool touchable = generatorSection (p, s);
        if ((! touchable || locks.isLocked (s)) && getParam (out, p) != getParam (base, p))
            return false;
    }
    for (int n : { 53, 54, 63 })
        if ((out[(size_t) n] & 0x08) != (base[(size_t) n] & 0x08))
            return false;
    return true;
}

static GenLocks randomLocks (std::mt19937& rng)
{
    GenLocks l;
    for (auto& b : l.locked)
        b = (rng() % 4) == 0;
    return l;
}

static void testSections()
{
    int touchable = 0;
    for (const auto& p : allParams())
    {
        GenSection s;
        if (generatorSection (p, s))
        {
            ++touchable;
            CHECK ((int) s >= 0 && (int) s < kNumGenSections);
        }
    }
    CHECK (touchable == 33);
    for (const char* id : kNeverTouched)
    {
        GenSection s;
        CHECK (findParam (id) != nullptr && ! generatorSection (*findParam (id), s));
    }
    for (int i = 0; i < kNumGenSections; ++i)
        CHECK (std::string (sectionName ((GenSection) i)).size() > 0);
    for (int i = 0; i < kNumFamilies; ++i)
        CHECK (std::string (familyName ((Family) i)).size() > 0);
}

// The rules must accept every factory voice, otherwise they would fight real sounds.
static void testFactoryPassesRules()
{
    const auto corpus = Corpus::withFactory();
    const auto env = Envelope::fromCorpus (corpus);
    for (int i = 0; i < kNumVoices; ++i)
    {
        const auto v = factoryVoice (i);
        const auto broken = Generator::ruleViolations (v, {}, false, &env);
        for (const auto& b : broken)
            std::printf ("factory voice %d breaks: %s\n", i, b.c_str());
        CHECK (broken.empty());
    }
}

static void testVary()
{
    const auto corpus = Corpus::withFactory();
    const auto env = Envelope::fromCorpus (corpus);
    std::mt19937 rng (1);

    for (int i = 0; i < 6000; ++i)
    {
        const auto base = factoryVoice ((int) (rng() % kNumVoices));
        const auto locks = randomLocks (rng);
        const double amount = (rng() % 1001) / 1000.0;
        Generator g ((std::uint32_t) rng());
        const auto out = g.vary (base, amount, locks, corpus);

        CHECK (isValidVoice (out, base));
        CHECK (respectsLocks (out, base, locks));
        const auto broken = Generator::ruleViolations (out, locks, amount > 0.75, &env, &base);
        if (! broken.empty()) std::printf ("vary %.2f: %s\n", amount, broken[0].c_str());
        CHECK (broken.empty());
    }

    // amount 0 changes nothing; same seed gives the same result
    const auto base = factoryVoice (12);
    Generator g0 (5);
    CHECK (g0.vary (base, 0.0, {}, corpus) == base);
    Generator a (42), b (42);
    CHECK (a.vary (base, 0.5, {}, corpus) == b.vary (base, 0.5, {}, corpus));

    // a small amount changes less than a big one
    auto changedCount = [&] (double amount)
    {
        double total = 0;
        for (int i = 0; i < 400; ++i)
        {
            Generator g ((std::uint32_t) (1000 + i));
            const auto out = g.vary (base, amount, {}, corpus);
            int n = 0;
            for (const auto& p : allParams())
                n += std::abs (getParam (out, p) - getParam (base, p));
            total += n;
        }
        return total / 400.0;
    };
    const double small = changedCount (0.1), big = changedCount (0.7);
    std::printf ("vary: mean total change %.1f at 10%%, %.1f at 70%%\n", small, big);
    CHECK (small * 3 < big);

    // everything locked: nothing moves
    GenLocks all;
    all.locked.fill (true);
    Generator gl (9);
    CHECK (gl.vary (base, 1.0, all, corpus) == base);
}

static void testCreate()
{
    const auto corpus = Corpus::withFactory();
    const auto env = Envelope::fromCorpus (corpus);
    std::mt19937 rng (2);

    double attack[kNumFamilies] {}, sustain[kNumFamilies] {}, freq[kNumFamilies] {};
    const int perFamily = 1500;

    for (int f = 0; f < kNumFamilies; ++f)
    {
        const auto family = (Family) f;
        for (int i = 0; i < perFamily; ++i)
        {
            const auto base = factoryVoice ((int) (rng() % kNumVoices));
            const auto locks = (i % 3 == 0) ? randomLocks (rng) : GenLocks {};
            Generator g ((std::uint32_t) rng());
            const auto out = g.create (base, family, locks, corpus);

            CHECK (isValidVoice (out, base));
            CHECK (respectsLocks (out, base, locks));
            const bool wild = family == Family::Texture;
            const auto broken = Generator::ruleViolations (out, locks, wild, &env, &base);
            if (! broken.empty()) std::printf ("%s: %s\n", familyName (family), broken[0].c_str());
            CHECK (broken.empty());

            if (! locks.isLocked (GenSection::Oscillators))
                CHECK (getParam (out, *findParam ("wave1")) != 5 || getParam (out, *findParam ("noiseBalance")) >= 150);
            if (! locks.isLocked (GenSection::Performance))
                CHECK (getParam (out, *findParam ("trigger")) != 5);   // never "Ext": it would be silent

            attack[f]  += getParam (out, *findParam ("volAttack"));
            sustain[f] += getParam (out, *findParam ("volSustain"));
            freq[f]    += getParam (out, *findParam ("filterFreq"));
        }
        attack[f] /= perFamily; sustain[f] /= perFamily; freq[f] /= perFamily;
        std::printf ("%-16s amp attack %5.1f  sustain %5.1f  cutoff %5.1f\n", familyName (family), attack[f], sustain[f], freq[f]);
    }

    const auto B = (int) Family::Bass, L = (int) Family::Lead, P = (int) Family::Pad, R = (int) Family::Percussive;
    CHECK (attack[P] > attack[B] + 40);
    CHECK (sustain[R] < 80 && sustain[B] > 150);
    CHECK (freq[B] < freq[L]);

    // determinism
    const auto base = factoryVoice (3);
    Generator a (77), b (77);
    CHECK (a.create (base, Family::Pad, {}, corpus) == b.create (base, Family::Pad, {}, corpus));
}

static void testBreed()
{
    const auto corpus = Corpus::withFactory();
    const auto env = Envelope::fromCorpus (corpus);
    std::mt19937 rng (3);

    // sections without cross-section rules come out exactly from one parent
    const GenSection pure[] = { GenSection::AmpEnv, GenSection::Modulation, GenSection::Performance, GenSection::Tauntek };

    int fromA = 0, fromB = 0;
    for (int i = 0; i < 3000; ++i)
    {
        const auto pa = factoryVoice ((int) (rng() % kNumVoices));
        const auto pb = factoryVoice ((int) (rng() % kNumVoices));
        const auto locks = randomLocks (rng);
        const double mutation = (i % 2 == 0) ? 0.0 : (rng() % 1001) / 1000.0;
        Generator g ((std::uint32_t) rng());
        const auto out = g.breed (pa, pb, mutation, locks, corpus);

        CHECK (isValidVoice (out, pa) || isValidVoice (out, pb));
        CHECK (respectsLocks (out, pa, locks));
        CHECK (Generator::ruleViolations (out, locks, false, &env, &pa).empty());

        if (mutation == 0.0)
            for (auto s : pure)
            {
                if (locks.isLocked (s))
                    continue;
                bool eqA = true, eqB = true;
                for (const auto& p : allParams())
                {
                    GenSection ps;
                    if (generatorSection (p, ps) && ps == s)
                    {
                        eqA = eqA && getParam (out, p) == getParam (pa, p);
                        eqB = eqB && getParam (out, p) == getParam (pb, p);
                    }
                }
                CHECK (eqA || eqB);
                if (eqA && ! eqB) ++fromA;
                if (eqB && ! eqA) ++fromB;
            }
    }
    std::printf ("breed: %d sections from A, %d from B\n", fromA, fromB);
    CHECK (fromA > 200 && fromB > 200);
}

// Random (but valid) user voices as base and corpus, including odd intervals.
static void testOddBases()
{
    std::mt19937 rng (4);
    std::vector<VoiceData> extra;
    for (int i = 0; i < 12; ++i)
    {
        auto v = factoryVoice ((int) (rng() % kNumVoices));
        for (const auto& p : allParams())
            if (rng() % 3 == 0)
                setParam (v, p, p.kind == ParamKind::Knob ? (int) (rng() % 255) : p.choices[rng() % p.choices.size()].value);
        extra.push_back (v);
    }
    const auto corpus = Corpus::withFactory (extra);
    const auto env = Envelope::fromCorpus (corpus);

    for (int i = 0; i < 3000; ++i)
    {
        const auto& base = extra[(size_t) (rng() % extra.size())];
        const auto locks = randomLocks (rng);
        Generator g ((std::uint32_t) rng());
        VoiceData out, other;
        bool wild = false;
        switch (i % 3)
        {
            case 0: { const double a = (rng() % 1001) / 1000.0; wild = a > 0.75; out = g.vary (base, a, locks, corpus); break; }
            case 1: { const auto f = (Family) (rng() % kNumFamilies); wild = f == Family::Texture; out = g.create (base, f, locks, corpus); break; }
            default: other = extra[(size_t) (rng() % extra.size())]; out = g.breed (base, other, 0.3, locks, corpus); break;
        }
        CHECK (isValidVoice (out, base));
        CHECK (respectsLocks (out, base, locks));
        auto broken = Generator::ruleViolations (out, locks, wild, &env, &base);
        if (i % 3 == 2 && ! broken.empty())   // breed: the other parent's interval is fine too
            broken = Generator::ruleViolations (out, locks, wild, &env, &other);
        if (! broken.empty() && failures < 40) std::printf ("odd base, mode %d: %s\n", i % 3, broken[0].c_str());
        CHECK (broken.empty());
    }
}

int main()
{
    testSections();
    testFactoryPassesRules();
    testVary();
    testCreate();
    testBreed();
    testOddBases();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
