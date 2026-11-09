// Sound generator for the OSCar: random, but within what real OSCar voices do.
//
// Three modes:
//  - vary   : small to wild changes around a voice
//  - create : a new voice from a family profile (bass, lead, pad, percussive, texture)
//  - breed  : each section from one of two parent voices, plus a little mutation
//
// What keeps it from doing "anything":
//  - learned limits: outside wild mode every knob stays inside the range used by
//    the corpus (factory bank + the user's own voices), and switch values are only
//    picked among values real voices use;
//  - family profiles: ranges and typical values per parameter, and whole sections
//    (e.g. an amp envelope) borrowed from a real voice that fits the family, so
//    parameters that belong together stay coherent;
//  - audibility rules (see ruleViolations) applied after every generation;
//  - musical intervals only for oscillator 2 (unison, fourth, fifth);
//  - locked sections and performance settings (tune, wheels, arpeggiator, function,
//    keyboard octave, gate time) are never touched, nor are unknown bits.
//
// Pure C++17 (no JUCE): unit-tested in Tests/GeneratorTests.cpp.

#pragma once

#include "OscarParams.h"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace oscar
{
enum class GenSection { Oscillators, Filter, FilterEnv, AmpEnv, Modulation, Performance, Tauntek, Count };
enum class Family { Bass, Lead, Pad, Percussive, Texture, Count };

constexpr int kNumGenSections = (int) GenSection::Count;
constexpr int kNumFamilies = (int) Family::Count;

const char* sectionName (GenSection s);
const char* familyName (Family f);

struct GenLocks
{
    std::array<bool, kNumGenSections> locked {};
    bool isLocked (GenSection s) const { return locked[(size_t) s]; }
};

// The section a parameter belongs to for the generator; false if the generator
// never touches it (tune, wheels, arpeggiator, keyboard octave, gate time).
bool generatorSection (const ParamDef& p, GenSection& out);

// Real voices to learn from. The factory bank is always part of it.
struct Corpus
{
    std::vector<VoiceData> voices;
    static Corpus withFactory (const std::vector<VoiceData>& extra = {});
};

// Per-knob limits learned from a corpus.
struct Envelope
{
    std::array<int, kVoiceSize> lo {}, hi {};   // indexed by ParamDef::offset (knobs only)
    static Envelope fromCorpus (const Corpus& c);
    int clamp (const ParamDef& p, int value) const;
};

class Generator
{
public:
    explicit Generator (std::uint32_t seed);

    // amount 0..1 (0 = no change); above 0.75 is "wild".
    VoiceData vary (const VoiceData& base, double amount, const GenLocks& locks, const Corpus& corpus);
    VoiceData create (const VoiceData& base, Family family, const GenLocks& locks, const Corpus& corpus);
    VoiceData breed (const VoiceData& a, const VoiceData& b, double mutation, const GenLocks& locks, const Corpus& corpus);

    // Names of the rules a voice breaks (empty = fine). Only sections that are
    // not locked are checked. 'wild' relaxes the learned limits. The base voice's own
    // oscillator 2 interval is always accepted (a variation never forces it back).
    static std::vector<std::string> ruleViolations (const VoiceData& v, const GenLocks& locks, bool wild,
                                                    const Envelope* learned, const VoiceData* base = nullptr);

private:
    double uniform();
    double gaussian();
    int    triangular (int lo, int hi, int mode);
    int    weighted (const std::vector<std::pair<int, double>>& choices);

    void enforceRules (VoiceData& v, const GenLocks& locks, bool wild, const Envelope& env, const VoiceData* base);

    std::mt19937 rng;
};

} // namespace oscar
