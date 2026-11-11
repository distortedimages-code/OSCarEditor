// Live functions on top of a voice, as in MIFM1: a random on any knob (a new value at each step,
// or values that come back the same every loop), and scales for the pitch (Transpose, Osc 2
// Interval, the notes played through the plugin). No JUCE inside: shared with the tests.

#pragma once

#include "OscarParams.h"

#include <cstdint>
#include <vector>

namespace oscar
{
//==============================================================================
// Scales: MIFM1's twelve-tone ones (its chord-following and microtonal scales need its sequencer
// or its own oscillators). Index 0 is Off.
struct ScaleDef
{
    const char* name;      // as printed in the display
    std::uint16_t mask;    // bit i: i semitones over the root; 0: no scale
};
const std::vector<ScaleDef>& scales();
const char* rootName (int root);   // C, C#, D ... B

bool inScale (int note, std::uint16_t mask, int root);
// The nearest note of the scale (a tie: the lower), as MIFM1's quantizer.
int quantizeNote (int note, std::uint16_t mask, int root);
// A transposition in semitones, lo..hi, so that the note 'ref' moved by it is in the scale: the
// nearest to 'semis' (a tie: the lower). 'semis' itself when there is no scale.
int nearestInterval (int semis, int ref, std::uint16_t mask, int root, int lo, int hi);
// The same for a knob moved from 'from' to 'to': the first in the scale from 'to' on, the way it
// moved (a step up never falls back); 'from' when the end comes first.
int stepInterval (int from, int to, int ref, std::uint16_t mask, int root, int lo, int hi);

// Tune Semi and Osc 2 Semi store 0 for +7 ... 14 for -7.
inline int semisOfValue (int value) { return 7 - value; }
inline int valueOfSemis (int semis) { return 7 - semis; }
bool isPitchInterval (const ParamDef&);   // Tune Semi, Osc 2 Semi

//==============================================================================
// Random: each control's amount, -100..100, how far it follows the shape below (100: the whole
// travel). Up (> 0) the shape as drawn, down (< 0) upside down, whatever the shape. Bipolar: the
// control moves either side of its own value; unipolar: only above it (up) or below it (down).
// 0: none, the control keeps its own value.
constexpr int kRandomMax = 100;

struct RandomRate
{
    const char* label;
    double beats;   // quarter notes per step (S&H, Smooth) or per cycle (the waves)
};
const std::vector<RandomRate>& randomRates();
int defaultRandomRate();                      // 1/16
const std::vector<int>& randomLoopLengths();  // 0 (no loop: new values), then 2 to 64 steps
constexpr int kDefaultLoopLength = 0;
// Free (not synced) rate: a step or a cycle every 10 ms to 10 s; a knob's 0..1, clockwise faster.
constexpr double kFreeRateMinMs = 10.0, kFreeRateMaxMs = 10000.0;
double freeRateMs (double knob);
double freeRateKnob (double ms);

// What the controls follow, as MIFM1's LFOs: S&H (a new value at each step) and Smooth (from one
// random value to the next) are random, each control its own (looped or not: see LOOP); the waves
// are one LFO that every control follows, on the DAW's bars when synced.
struct ModShape
{
    const char* name;      // as printed in the display
    bool random;
};
const std::vector<ModShape>& modShapes();
enum ModShapeIndex { kShapeSampleHold = 0, kShapeSmooth = 1 };
// A wave at a phase of its cycle (0..1), 0..1 (the randoms: 0.5).
double waveValue (int shape, double phase);

bool isRandomisable (const ParamDef&);        // the knobs, Transpose, Osc 2 Interval and Osc 2 Octave

// The value for x (0..1, the shape's height): bipolar, 'base' + amount % of the travel times
// (2x - 1); unipolar, 'base' + amount % times x. Kept within the travel.
int randomValue (const ParamDef&, int base, int amount, double x, bool bipolar = true);

// The looped random, 0..1: always the same for this seed, parameter and step of the loop.
double loopRandom (std::uint32_t seed, int paramIndex, int step);
}
