// Interface language. The code is written in English; Resources/Lang/fr.txt maps each
// English text to French (JUCE LocalisedStrings format). The names of the OSCar's controls
// (Detune, Separation, Q, Glide Type...) and of its keys (INSERT, VOICE, SPACE...) stay in
// English on purpose, so they match the synth and the Tauntek documentation.
// Tests/check_translations.py checks that every text used in the code has a translation.

#pragma once

#include <JuceHeader.h>

namespace lang
{
// "fr" or "en". Applies to the whole plug-in (all instances share it).
void apply (const juce::String& code);
juce::String current();

// Translates 'text' then fills %1, %2... with 'args'.
inline juce::String tr (const juce::String& text, std::initializer_list<juce::String> args = {})
{
    auto s = juce::translate (text);
    int i = 1;
    for (const auto& a : args)
        s = s.replace ("%" + juce::String (i++), a);
    return s;
}

// Capitals for labels. String::toUpperCase depends on the C library's locale, which in a
// plug-in host often leaves accented letters alone ("GéNéRATEUR"), so Latin-1 letters and
// the French ligature are done here.
inline juce::String upper (const juce::String& text)
{
    juce::String out;
    out.preallocateBytes (text.getNumBytesAsUTF8() + 8);
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        auto c = *p;
        if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 0x20;   // à..þ -> À..Þ
        else if (c == 0x153) c = 0x152;                       // œ -> Œ
        else if (c == 0xFF)  c = 0x178;                       // ÿ -> Ÿ
        else                 c = juce::CharacterFunctions::toUpperCase (c);
        out += juce::String::charToString (c);
    }
    return out;
}

// Keeps the translation table alive while a plug-in instance exists, and removes it when the
// last one goes (so nothing is left over when the host unloads the plug-in).
struct Holder
{
    ~Holder();
};
}
