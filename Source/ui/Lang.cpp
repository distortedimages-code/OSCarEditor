#include "Lang.h"

#include "BinaryData.h"

namespace lang
{
namespace
{
    juce::String& currentCode()
    {
        static juce::String code = "en";
        return code;
    }
}

void apply (const juce::String& code)
{
    if (code == "fr")
    {
        const auto text = juce::String::fromUTF8 (BinaryData::fr_txt, BinaryData::fr_txtSize);
        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (text, false));
        currentCode() = "fr";
    }
    else
    {
        juce::LocalisedStrings::setCurrentMappings (nullptr);
        currentCode() = "en";
    }
}

juce::String current()
{
    return currentCode();
}

Holder::~Holder()
{
    juce::LocalisedStrings::setCurrentMappings (nullptr);
    currentCode() = "en";
}
}
