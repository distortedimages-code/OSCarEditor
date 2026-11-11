#include "Performance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace oscar
{
const std::vector<ScaleDef>& scales()
{
    // MIFM1's masks (engine.cc, kScaleMasks): bit i is i semitones over the root
    static const std::vector<ScaleDef> s {
        { "OFF", 0x000 },        { "MAJOR", 0xAB5 },      { "MINOR", 0x5AD },      { "DORIAN", 0x6AD },
        { "PHRYGIAN", 0x5AB },   { "LYDIAN", 0xAD5 },     { "MIXOLYDIAN", 0x6B5 }, { "LOCRIAN", 0x56B },
        { "HARM MINOR", 0x9AD }, { "MEL MINOR", 0xAAD },  { "MAJ PENTA", 0x295 },  { "MIN PENTA", 0x4A9 },
        { "BLUES", 0x4E9 },      { "WHOLE TONE", 0x555 }, { "HIRAJOSHI", 0x18D },  { "PHRYG DOM", 0x5B3 },
    };
    return s;
}

const char* rootName (int root)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((root % 12) + 12) % 12];
}

bool inScale (int note, std::uint16_t mask, int root)
{
    if (mask == 0)
        return true;
    const int rel = ((note - root) % 12 + 12) % 12;
    return ((mask >> rel) & 1u) != 0;
}

int quantizeNote (int note, std::uint16_t mask, int root)
{
    if (mask == 0)
        return note;
    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (note - d, mask, root)) return note - d;
        if (inScale (note + d, mask, root)) return note + d;
    }
    return note;
}

int nearestInterval (int semis, int ref, std::uint16_t mask, int root, int lo, int hi)
{
    semis = std::clamp (semis, lo, hi);
    if (mask == 0)
        return semis;
    for (int d = 0; d <= hi - lo; ++d)
    {
        if (semis - d >= lo && inScale (ref + semis - d, mask, root)) return semis - d;
        if (semis + d <= hi && inScale (ref + semis + d, mask, root)) return semis + d;
    }
    return semis;
}

int stepInterval (int from, int to, int ref, std::uint16_t mask, int root, int lo, int hi)
{
    to = std::clamp (to, lo, hi);
    if (mask == 0 || to == from)
        return to;
    const int dir = to > from ? 1 : -1;
    for (int s = to; s >= lo && s <= hi; s += dir)
        if (inScale (ref + s, mask, root))
            return s;
    return from;
}

bool isPitchInterval (const ParamDef& p)
{
    return std::strcmp (p.id, "tuneSemi") == 0 || std::strcmp (p.id, "osc2Semi") == 0;
}

//==============================================================================
const std::vector<RandomRate>& randomRates()
{
    static const std::vector<RandomRate> r {
        { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 },
        { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/2", 2.0 }, { "1/1", 4.0 }, { "2/1", 8.0 }, { "4/1", 16.0 },
        { "8/1", 32.0 }, { "16/1", 64.0 },
    };
    return r;
}

const std::vector<ModShape>& modShapes()
{
    static const std::vector<ModShape> s {
        { "S&H", true }, { "SMOOTH", true }, { "SINE", false }, { "TRIANGLE", false }, { "SAW", false }, { "RAMP", false },
        { "SQUARE", false }, { "PUMP", false }, { "STAIRS", false }, { "BOUNCE", false }, { "CLAVE", false },
        { "RISES", false }, { "FALLS", false }, { "ZIGZAG", false },
    };
    return s;
}

double waveValue (int shape, double ph)
{
    // MIFM1's LFO shapes (engine.cc, lfo_figure), -1..1, then 0..1
    constexpr double pi = 3.14159265358979323846;
    ph = ph - std::floor (ph);
    const double u = 8.0 * ph;                  // the eighths of the cycle (3 + 3 + 2)
    const double s = u < 3.0 ? 0.0 : u < 6.0 ? 3.0 : 6.0, len = u < 6.0 ? 3.0 : 2.0;
    double v = 0.0;
    switch (shape)
    {
        case 2:  v = std::sin (2.0 * pi * ph); break;                               // SINE
        case 3:  v = ph < 0.25 ? 4.0 * ph : ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0; break;   // TRIANGLE
        case 4:  v = 1.0 - 2.0 * ph; break;                                         // SAW: falls
        case 5:  v = -1.0 + 2.0 * ph; break;                                        // RAMP: rises
        case 6:  v = ph < 0.5 ? 1.0 : -1.0; break;                                  // SQUARE
        case 7:                                                                     // PUMP: down at once, swelling back
        {
            const double x = ph < 0.7 ? ph / 0.7 : 1.0;
            v = -1.0 + 2.0 * x * x * (3.0 - 2.0 * x);
            break;
        }
        case 8:  v = -1.0 + (2.0 / 3.0) * std::floor (std::min (3.0, ph * 4.0)); break;   // STAIRS
        case 9:                                                                     // BOUNCE: arcs, each shorter and lower
        {
            double t = 0.0, l = 0.4, h = 1.0;
            for (int k = 0; k < 12 && ph >= t + l; ++k)
            {
                t += l;
                l *= 0.6;
                h *= 0.36;
            }
            const double x = (ph - t) / l;
            v = -1.0 + 8.0 * h * x * (1.0 - x);
            break;
        }
        case 10:                                                                    // CLAVE: hits on 3 + 3 + 2
        {
            const double x = u - s;
            const double d = x >= 1.5 ? 0.0 : 1.0 - x / 1.5;
            v = -1.0 + 2.0 * d * d;
            break;
        }
        case 11: v = -1.0 + 2.0 * (u - s) / len; break;                             // RISES, on 3 + 3 + 2
        case 12: v = 1.0 - 2.0 * (u - s) / len; break;                              // FALLS
        case 13:                                                                    // ZIGZAG
        {
            static const double xs[5] = { 0.0, 3.0, 4.0, 6.0, 8.0 }, ys[5] = { -1.0, 1.0, -0.2, 0.6, -1.0 };
            const int i = u < 3.0 ? 0 : u < 4.0 ? 1 : u < 6.0 ? 2 : 3;
            v = ys[i] + (ys[i + 1] - ys[i]) * (u - xs[i]) / (xs[i + 1] - xs[i]);
            break;
        }
        default: v = 0.0; break;                                                    // the randoms
    }
    return 0.5 + 0.5 * std::clamp (v, -1.0, 1.0);
}

int defaultRandomRate() { return 2; }

const std::vector<int>& randomLoopLengths()
{
    static const std::vector<int> l = []
    {
        std::vector<int> v { 0 };
        for (int n = 2; n <= 64; ++n)
            v.push_back (n);
        return v;
    }();
    return l;
}

namespace
{
    // the values a random moves through
    std::pair<int, int> travel (const ParamDef& p)
    {
        if (p.kind == ParamKind::Knob)
            return { 0, kKnobMaxEditable };
        int lo = 1000, hi = -1000;
        for (const auto& c : p.choices)
        {
            lo = std::min (lo, c.value);
            hi = std::max (hi, c.value);
        }
        return { lo, hi };
    }
}

bool isRandomisable (const ParamDef& p)
{
    return p.cc >= 0 && (p.kind == ParamKind::Knob || isPitchInterval (p) || std::strcmp (p.id, "octaveShift") == 0);
}

int randomValue (const ParamDef& p, int base, int amount, double x, bool bipolar)
{
    const auto [lo, hi] = travel (p);
    const double a = std::clamp (amount, -kRandomMax, kRandomMax) / (double) kRandomMax;
    const double v = base + a * (bipolar ? 2.0 * x - 1.0 : x) * (hi - lo);
    return std::clamp ((int) std::lround (v), lo, hi);
}

double freeRateMs (double knob)
{
    knob = std::clamp (knob, 0.0, 1.0);
    return kFreeRateMaxMs * std::pow (kFreeRateMinMs / kFreeRateMaxMs, knob);
}

double freeRateKnob (double ms)
{
    ms = std::clamp (ms, kFreeRateMinMs, kFreeRateMaxMs);
    return std::log (ms / kFreeRateMaxMs) / std::log (kFreeRateMinMs / kFreeRateMaxMs);
}

double loopRandom (std::uint32_t seed, int paramIndex, int step)
{
    // MIFM1's loop01: a hash of the seed, the step and the parameter
    std::uint32_t h = (seed + 1u) * 2654435761u ^ (std::uint32_t) step * 40503u ^ (std::uint32_t) (paramIndex * 9973 + 7919);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (double) (h & 0xFFFFu) / 65535.0;
}
}
