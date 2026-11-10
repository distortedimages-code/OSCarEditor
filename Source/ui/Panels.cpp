#include "Panels.h"

#include "../PluginEditor.h"

using namespace oscar;
using namespace atelier;
using lang::tr;

namespace panels
{
namespace
{
    const ParamDef& def (const char* id)
    {
        const auto* p = findParam (id);
        jassert (p != nullptr);
        return *p;
    }

    // The words printed on the OSCar's panel (in English, as on the synth), so that going from the
    // plugin to the synth and back reads the same. Tauntek settings keep the Tauntek chart's name.
    juce::String panelWord (const ParamDef& p)
    {
        static const std::map<std::string, const char*> words {
            { "wave1", "Osc 1 Waveform" }, { "wave2", "Osc 2 Waveform" }, { "octaveShift", "Octave Shift" },
            { "osc2Semi", "Osc 2 Interval" }, { "tuneSemi", "Transpose" }, { "detune", "De Tune" },
            { "pulseWidth", "PW" }, { "oscBalance", "Osc Balance" }, { "noiseBalance", "Noise Balance" },
            { "bendAmount", "Bend Amount" }, { "mwPitch", "Pitch Amount" }, { "mwFilter", "Filter Amount" },
            { "octaveSet", "Octave" }, { "glideType", "Type" }, { "glideRate", "Time" },
            { "filterFreq", "Frequency" }, { "resonance", "Q" }, { "filterSep", "Separation" },
            { "envFilter", "Env Amount" }, { "filterDrive", "Filter Drive" },
            { "lfoWave", "Waveform" }, { "lfoRate", "Rate" }, { "introDelay", "Intro" }, { "lfoPitch", "Pitch Mod" },
            { "lfoFilter", "LFO Amount" }, { "trigger", "Triggering" }, { "function", "Function" }, { "gateTime", "Gate Time" },
            { "filtAttack", "Attack" }, { "filtDecay", "Decay" }, { "filtSustain", "Sustain Level" }, { "filtRelease", "Release" },
            { "volAttack", "Attack" }, { "volDecay", "Decay" }, { "volSustain", "Sustain Level" }, { "volRelease", "Release" },
        };
        const auto it = words.find (p.id);
        return it != words.end() ? juce::String (it->second) : juce::String (p.name);
    }

    juce::String switchLabel (const ParamDef& p)
    {
        return panelWord (p) + (p.labelsVerified ? "" : " ?");
    }

    juce::String unverifiedText() { return tr ("Values checked against the Tauntek firmware; the names come from community documentation and are not confirmed yet."); }

    // MIDI clock (Tauntek firmware, routine 0x0A35)
    constexpr int kExtTrigger = 5;   // Trigger "Ext": arp and sequencer step on the MIDI clock
    // clock ticks per step {1, 2, 3, 4, 6, 8, 12, 16} at 24 per quarter note, chosen by Tempo >> 5
    const char* const kDivisions[8] = { "1/96", "1/48", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T" };
    const char* const kDivides[8] = { "1", "2", "3", "4", "6", "8", "12", "16" };   // as printed on the panel

    void drawText (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, float size = 13.0f,
                   juce::Colour c = colours::dim, juce::Justification j = juce::Justification::topLeft)
    {
        g.setFont (Fonts::label (size));
        g.setColour (c);
        g.drawFittedText (text, area.toNearestInt(), j, 6, 1.0f);
    }

    // Places 'items' (fixed size) evenly across 'row'.
    void spread (std::initializer_list<juce::Component*> items, juce::Rectangle<int> row, int itemW, int itemH)
    {
        const int n = (int) items.size();
        if (n == 0) return;
        const float step = n > 1 ? (float) (row.getWidth() - itemW) / (float) (n - 1) : 0.0f;
        int i = 0;
        for (auto* c : items)
        {
            const int x = n > 1 ? row.getX() + juce::roundToInt (step * (float) i) : row.getCentreX() - itemW / 2;
            c->setBounds (x, row.getY(), itemW, itemH);
            ++i;
        }
    }

    std::vector<juce::Rectangle<float>> splitColumns (juce::Rectangle<float> area, std::initializer_list<float> widths)
    {
        float total = 0;
        for (auto w : widths) total += w;
        const float gap = (area.getWidth() - total) / (float) juce::jmax (1, (int) widths.size() - 1);
        std::vector<juce::Rectangle<float>> cols;
        float x = area.getX();
        for (auto w : widths)
        {
            cols.push_back ({ x, area.getY(), w, area.getHeight() });
            x += w + gap;
        }
        return cols;
    }

    juce::String voiceItemText (const BankDocument& doc, int slot)
    {
        return juce::String (slotLabel (slot)).paddedLeft (' ', 3) + "  " + doc.getDisplayName (slot);
    }
}

//==============================================================================
// Wear
//==============================================================================
void setupWear (juce::Component& page, WearLayer& wear, std::vector<juce::Rectangle<float>> plates,
                std::vector<juce::Rectangle<float>> displays, std::vector<WearLayer::Spot> handled, std::vector<juce::Path> extraKeepClear)
{
    auto background = [plates] (juce::Point<float> p)
    {
        for (const auto& c : plates)
            if (c.contains (p))
                return colours::inset.interpolatedWith (colours::insetLow, juce::jlimit (0.0f, 1.0f, (p.y - c.getY()) / c.getHeight()));
        return colours::panel;
    };
    std::vector<juce::Path> keep = std::move (extraKeepClear);
    auto rect = [&keep] (juce::Rectangle<float> r) { juce::Path p; p.addRoundedRectangle (r, 3.0f); keep.push_back (p); };
    for (auto* c : page.getChildren())
        if (c != &wear && c->isVisible())
            rect (c->getBounds().toFloat().expanded (2.0f));
    for (const auto& d : displays)
        rect (d.expanded (2.0f));
    wear.setBounds (page.getLocalBounds());
    wear.setup (wearLevel(), background, std::move (keep), std::move (handled), std::move (plates));
    wear.toFront (false);
}

//==============================================================================
// Voice
//==============================================================================
VoicePanel::VoicePanel (OscarProcessor& p) : proc (p)
{
    auto edit = [this] (const char* id) { return [this, id] (int v) { proc.editParam (proc.getSelectedSlot(), def (id), v); }; };

    auto addKnob = [&] (const char* id, KnobStyle style)
    {
        if (currentFinish() == 1 && style == KnobStyle::Chrome)
            style = KnobStyle::Knurled;   // the black finish: black knobs throughout
        auto k = std::make_unique<Knob> (panelWord (def (id)), style);
        k->onChange = edit (id);
        k->setTooltip (tr ("%1: stored value 0-254 (sent as CC %2 in Live CC).", { juce::String (def (id).name), juce::String (def (id).cc) }));
        addAndMakeVisible (*k);
        knobs[id] = std::move (k);
    };
    for (auto* id : { "detune", "pulseWidth", "oscBalance", "noiseBalance", "glideRate", "gateTime" })
        addKnob (id, KnobStyle::Knurled);
    for (auto* id : { "bendAmount", "mwPitch", "mwFilter", "resonance", "filterSep", "envFilter", "filterDrive",
                      "lfoRate", "introDelay", "lfoPitch", "lfoFilter" })
        addKnob (id, KnobStyle::Chrome);
    addKnob ("filterFreq", KnobStyle::Brass);
    for (auto* id : { "filterFreq", "resonance", "filterSep", "detune", "lfoRate" })   // the main controls: their names framed
        knob (id).setLabelStyle (LabelStyle::Boxed);
    knob ("filterDrive").setPlate (true);   // the drive stands out: the whole knob on an orange plate, printed in reverse
    knob ("filterFreq").setLedArc (true);      // an LED band along the cutoff's travel, brighter as it opens
    knob ("oscBalance").setScale ("1", "2", "=");   // equal mix in the middle, as printed on the panel
    knob ("noiseBalance").setScale ("OSC", "NOISE", "=");
    // bipolar, as on the OSCar's panel: zero in the middle (stored 128), shown signed
    for (auto* id : { "detune", "envFilter", "lfoPitch", "lfoFilter" })
    {
        knob (id).setScale (juce::String::fromUTF8 ("\xe2\x88\x92"), "+", "0");
        knob (id).setValueText ([] (int v) { return juce::String (v - 128); });
    }

    // both envelopes as faders, side by side
    const std::pair<const char*, const char*> faderIds[] = { { "filtAttack", "Att" }, { "filtDecay", "Dec" }, { "filtSustain", "Sus" },
                                                             { "filtRelease", "Rel" }, { "volAttack", "Att" }, { "volDecay", "Dec" },
                                                             { "volSustain", "Sus" }, { "volRelease", "Rel" } };
    for (const auto& [id, label] : faderIds)
    {
        auto f = std::make_unique<Fader> (label);
        f->onChange = edit (id);
        auto tip = tr ("%1: stored value 0-254 (sent as CC %2 in Live CC).", { juce::String (def (id).section) + " " + def (id).name, juce::String (def (id).cc) });
        if (std::strcmp (id, "filtSustain") == 0 || std::strcmp (id, "filtRelease") == 0)
            tip << "\n" << tr ("DELAY: with the filter envelope's Sustain at 0, a short blip comes back at the end of the sound; Release sets how long after the decay.");
        f->setTooltip (tip);
        addAndMakeVisible (*f);
        faders[id] = std::move (f);
    }

    auto tipFor = [] (const ParamDef& d) { return d.labelsVerified ? tr ("Values checked against the Tauntek firmware, names against the Tauntek chart or the OSCar's panel.") : unverifiedText(); };
    // Switches: a short printed legend per position; the full names are in the list (rotary) or the tooltip.
    // Four positions or more: a rotary switch (an empty legend is printed only when chosen).
    // Two or three: a button that steps through them, with an LED and a legend for each.
    struct Legend { int value; const char* legend; };
    auto itemsOf = [] (const ParamDef& d, const std::vector<Legend>& legends)
    {
        std::vector<RotarySwitch::Item> items;
        for (const auto& l : legends)
        {
            juce::String name = juce::String (l.value);
            for (const auto& c : d.choices)
                if (c.value == l.value)
                    name = c.label;
            items.push_back ({ l.value, l.legend, name });
        }
        return items;
    };
    auto addRotary = [&] (const char* id, std::vector<Legend> legends, float span = 0.0f, float legendSize = 10.5f)
    {
        const auto& d = def (id);
        auto r = std::make_unique<RotarySwitch> (switchLabel (d));
        r->setItems (itemsOf (d, legends), span);
        r->setLegendSize (legendSize);
        r->onChange = edit (id);
        r->setTooltip (tipFor (d));
        addAndMakeVisible (*r);
        rotaries[id] = std::move (r);
    };
    auto addLeds = [&] (const char* id, std::vector<Legend> legends, const juce::String& label, std::function<void (int)> onChange = {})
    {
        const auto& d = def (id);
        auto l = std::make_unique<LedSelector> (label);
        l->setItems (itemsOf (d, legends));
        l->onChange = onChange ? onChange : edit (id);
        l->setTooltip (tipFor (d));
        addAndMakeVisible (*l);
        ledSelectors[id] = std::move (l);
    };
    auto semitones = [] { std::vector<Legend> l;   // value 0 = +7 ... 14 = -7, shown from -7 to +7
                          for (int v = 14; v >= 0; --v) l.push_back ({ v, v == 14 ? "-7" : v == 7 ? "0" : v == 0 ? "+7" : "" });
                          return l; };
    // the waveforms as on the panel: N, ramp, square, pulse, PWM, off (osc 2: copies osc 1); the
    // numbered waveforms -3 to -12 point the knob at N and show their number under it
    for (auto* id : { "wave1", "wave2" })
    {
        addRotary (id, { { 0, "N" }, { 1, "~saw" }, { 2, "~sqr" }, { 3, "~pulse" }, { 4, "PWM" },
                         { 5, std::strcmp (id, "wave1") == 0 ? "OFF" : "OSC\n1" } }, 0.0f, 10.0f);
        std::vector<RotarySwitch::Item> numbered;
        for (const auto& c : def (id).choices)
            if (c.value >= 6)
                numbered.push_back ({ c.value, juce::String (3 - c.value), c.label });   // 6..15 = -3..-12
        rotary (id).setMoreValues (numbered, 0);
    }
    addRotary ("octaveShift", { { 0, "-2" }, { 1, "-1" }, { 2, "0" }, { 3, "+1" }, { 4, "+2" }, { 5, "+3" } });   // as on the panel
    addRotary ("osc2Semi", semitones());
    addRotary ("tuneSemi", semitones());
    // the keyboard octave: five LEDs, as on the synth
    octaveLeds.setLabel (switchLabel (def ("octaveSet")));
    {
        std::vector<RotarySwitch::Item> octaves;
        for (const auto& c : def ("octaveSet").choices)
            octaves.push_back ({ c.value, {}, c.label });
        octaveLeds.setItems (octaves);
    }
    octaveLeds.onChange = edit ("octaveSet");
    octaveLeds.setTooltip (tipFor (def ("octaveSet")));
    addAndMakeVisible (octaveLeds);
    addRotary ("pwmRate", { { 0, "1" }, { 1, "2" }, { 2, "3" }, { 3, "4" } });   // slowest to fastest (names in the list)
    addRotary ("lfoWave", { { 0, "N" }, { 1, "~saw" }, { 2, "~sqr" }, { 3, "FILTER\nENV" }, { 4, "KBD" }, { 5, "R" } }, 0.0f, 9.5f);
    addRotary ("trigger", { { 0, "SG" }, { 1, "MULT" }, { 2, "RPT1" }, { 3, "RPT2" }, { 4, "RPT1\n& 2" }, { 5, "EXT" } }, 0.0f, 10.0f);

    // under the ARP title: Direction and Mode (Arp Direction and Arp Mode on the panel)
    auto underArp = [] (const ParamDef& d, const char* word) { return juce::String (word) + (d.labelsVerified ? "" : " ?"); };
    addLeds ("arpDir", { { 1, "DOWN" }, { 2, "UP" }, { 3, "UP/DN" } }, underArp (def ("arpDir"), "Direction"));
    addLeds ("arpMode", { { 0, "NORM" }, { 1, "DEL" }, { 2, "MEM" } }, underArp (def ("arpMode"), "Mode"));
    // Function, Glide type and Filter type as on the panel: one knob with six positions, the last
    // three under a bracket (HOLD, FIX, NO TRACK) - that half is the flag bit (values 4-6)
    addRotary ("function", { { 0, "NORM" }, { 1, "ARP" }, { 2, "DUO" }, { 4, "N" }, { 5, "A" }, { 6, "D" } }, 0.0f, 9.5f);
    addRotary ("glideType", { { 0, "N" }, { 1, "AUTO" }, { 2, "GLISS" }, { 4, "N" }, { 5, "A" }, { 6, "G" } }, 0.0f, 9.5f);
    addRotary ("filterType", { { 0, "LP" }, { 1, "BP" }, { 2, "HP" }, { 4, "LP" }, { 5, "BP" }, { 6, "HP" } }, 0.0f, 9.5f);
    for (const auto& [id, name] : { std::pair<const char*, const char*> { "function", "HOLD" }, { "glideType", "FIX" }, { "filterType", "NO TRACK" } })
    {
        rotary (id).setBracket (3, name);
        rotary (id).setExtraTop (12);
    }
    rotary ("lfoWave").setExtraTop (8);   // its two-line FILTER ENV at the top

    lfoToOsc1.setTooltip (tr ("Tauntek: the LFO also modulates oscillator 1 (same as TUNE/TRANSPOSE + the 'LFO to Oscillator 1' keys)."));
    lfoSync.setTooltip (tr ("Tauntek: LFO hard sync (same as TUNE/TRANSPOSE + the 'LFO Sync' keys)."));
    lfoToOsc1.onClick = [this] { proc.editParam (proc.getSelectedSlot(), def ("lfoToOsc1"), paramValue ("lfoToOsc1") == 0 ? 1 : 0); };
    lfoSync.onClick = [this] { proc.editParam (proc.getSelectedSlot(), def ("lfoSync"), paramValue ("lfoSync") == 0 ? 1 : 0); };
    addAndMakeVisible (lfoToOsc1);
    addAndMakeVisible (lfoSync);

    tempo.setTooltip (tr ("Global tempo (CC 54, sent with Live CC). Not stored in voices; can be automated from the DAW. When the voice follows the MIDI clock, it sets the division. In Internal, below 32 the arpeggiator steps on every MIDI clock tick.")
                      + "\n" + tr ("Drag up or down on the number, or use the mouse wheel."));
    tempoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getHostParameters(), "tempo", tempo.slider);
    tempo.setValueText ([this] (int t)
    {
        if (followsClock())
            return juce::String (kDivisions[juce::jlimit (0, 7, t >> 5)]);
        return juce::String (t);
    });
    addAndMakeVisible (tempo);

    // Firmware (routine 0x0A35): with Trigger = Ext the arpeggiator and the sequencer step every
    // {1, 2, 3, 4, 6, 8, 12, 16} MIDI clock ticks (24 per quarter note), chosen by Tempo >> 5.
    syncLeds.setItems ({ { 0, "INT", tr ("Internal") }, { 1, "MIDI", tr ("MIDI clock") } });
    syncLeds.setTooltip (tr ("MIDI clock: the arpeggiator and the sequencer step on the MIDI clock the OSCar receives (Trigger = Ext, stored in the voice). The clock can come from the DAW (Setup page) or from any MIDI device. Internal: they follow the Tempo knob."));
    syncLeds.onChange = [this] (int v) { setSync (v); };
    std::vector<RotarySwitch::Item> divisions;
    for (int z = 0; z < 8; ++z)
        divisions.push_back ({ z, kDivides[z], juce::String (kDivides[z]) + "  (" + kDivisions[z] + ")" });
    divisionSwitch.setItems (divisions, 270.0f);
    divisionSwitch.setLegendSize (9.5f);
    divisionSwitch.setTooltip (tr ("Steps of the arpeggiator and the sequencer when the voice follows the MIDI clock (T = triplet). Sets the global Tempo (CC 54, sent with Live CC)."));
    divisionSwitch.onChange = [this] (int z)
    {
        if (tempoWatch != nullptr)
            tempoWatch->setValueAsCompleteGesture ((float) (z * 32 + 16));
    };
    addAndMakeVisible (syncLeds);
    addAndMakeVisible (divisionSwitch);
    if (auto* t = proc.getHostParameters().getParameter ("tempo"))
    {
        tempoWatch = std::make_unique<juce::ParameterAttachment> (*t, [this] (float) { refreshClock(); });
        tempoWatch->sendInitialUpdate();
    }

    addAndMakeVisible (wear);
    layout();
    setupWear (wearLevel());
}

void VoicePanel::setupWear (int level)
{
    // the panel's colour under a point: the column's gradient (as drawInset paints it)
    auto cols = columns;
    auto background = [cols] (juce::Point<float> p)
    {
        for (const auto& c : cols)
            if (c.contains (p))
                return colours::inset.interpolatedWith (colours::insetLow, juce::jlimit (0.0f, 1.0f, (p.y - c.getY()) / c.getHeight()));
        return colours::panel;
    };

    // what the wear must not touch: knobs, switch knobs, buttons, LEDs, windows and readouts
    std::vector<juce::Path> keep;
    auto rect = [&keep] (juce::Rectangle<float> r) { juce::Path p; p.addRoundedRectangle (r, 3.0f); keep.push_back (p); };
    auto circle = [&keep] (juce::Rectangle<float> r) { juce::Path p; p.addEllipse (r); keep.push_back (p); };
    for (auto* c : getChildren())
    {
        if (c == &wear || c == &lfoStack)
            continue;
        const auto at = c->getPosition().toFloat();
        if (auto* k = dynamic_cast<Knob*> (c))
        {
            circle (k->slider.getBounds().toFloat().translated (at.x, at.y).expanded (k->getStyle() == KnobStyle::Brass ? 0.0f : 2.0f));
            rect (k->readoutBounds().translated (at.x, at.y).expanded (1.0f));
            if (k->hasPlate())   // the orange plate stays clean
                rect (k->plateBounds().translated (at.x, at.y).expanded (1.0f));
        }
        else if (auto* f = dynamic_cast<Fader*> (c))
        {
            const auto s = f->slider.getBounds().toFloat().translated (at.x, at.y);
            rect (juce::Rectangle<float> (s.getCentreX() - 11.0f, s.getY() - 8.0f, 28.0f, s.getHeight() + 16.0f));
            rect (f->readoutBounds().translated (at.x, at.y).expanded (1.0f));
        }
        else if (auto* r = dynamic_cast<RotarySwitch*> (c))
            circle (r->knobBounds().translated (at.x, at.y).expanded (3.0f));
        else if (auto* l = dynamic_cast<LedSelector*> (c))
            rect (l->controlBounds().translated (at.x, at.y).expanded (2.0f));
        else if (dynamic_cast<Selector*> (c) != nullptr || dynamic_cast<VfdNumber*> (c) != nullptr)
        {
            juce::Rectangle<int> body;
            for (auto* inner : c->getChildren())
                body = body.isEmpty() ? inner->getBounds() : body.getUnion (inner->getBounds());
            rect (body.toFloat().translated (at.x, at.y).expanded (2.0f));
        }
        else
            rect (c->getBounds().toFloat().expanded (2.0f));   // buttons
    }

    // handled most: the filter, the main knobs, the waveforms, trigger, hold and tempo
    std::vector<WearLayer::Spot> spots;
    auto spot = [&spots] (juce::Component& c, float radius) { spots.push_back ({ c.getBounds().toFloat().getCentre(), radius }); };
    auto knobSpot = [&spots] (Knob& k, float radius) { spots.push_back ({ k.getPosition().toFloat() + k.slider.getBounds().toFloat().getCentre(), radius }); };
    knobSpot (knob ("filterFreq"), 95.0f);
    for (auto* id : { "resonance", "filterSep", "detune", "lfoRate", "glideRate" })
        knobSpot (knob (id), 48.0f);
    spot (rotary ("wave1"), 50.0f);
    spot (rotary ("wave2"), 44.0f);
    spot (rotary ("trigger"), 48.0f);
    spot (rotary ("function"), 40.0f);
    spot (tempo, 50.0f);
    spot (*faders.at ("volRelease"), 30.0f);

    wear.setBounds (0, 0, width, height);
    wear.setup (level, background, std::move (keep), std::move (spots), cols);
    wear.toFront (false);
}

VoicePanel::~VoicePanel()
{
    tempoWatch.reset();
    tempoAttachment.reset();
}

int VoicePanel::paramValue (const char* id) const
{
    return getParam (proc.getDocument().getVoice (proc.getSelectedSlot()), def (id));
}

void VoicePanel::setSync (int v)
{
    const int slot = proc.getSelectedSlot();
    const int trigger = paramValue ("trigger");
    if (v == 1 && trigger != kExtTrigger)
    {
        lastTrigger = trigger;
        proc.editParam (slot, def ("trigger"), kExtTrigger);
    }
    else if (v == 0 && trigger == kExtTrigger)
    {
        proc.editParam (slot, def ("trigger"), lastTrigger);
    }
    refreshClock();
}

void VoicePanel::editFlagged (const char* id, int base, bool flag)
{
    proc.editParam (proc.getSelectedSlot(), def (id), juce::jlimit (0, 2, base) + (flag ? 4 : 0));
}

bool VoicePanel::followsClock() const
{
    return paramValue ("trigger") == kExtTrigger;
}

void VoicePanel::refreshClock()
{
    const bool ext = followsClock();
    const int t = juce::roundToInt (proc.getHostParameters().getRawParameterValue ("tempo")->load());
    syncLeds.setValue (ext ? 1 : 0);
    divisionSwitch.setValue (juce::jlimit (0, 7, t >> 5));
    divisionSwitch.setAlpha (ext ? 1.0f : 0.45f);
    tempo.repaint();
}

Knob& VoicePanel::knob (const char* id)              { return *knobs.at (id); }
Selector& VoicePanel::selector (const char* id)      { return *selectors.at (id); }
RotarySwitch& VoicePanel::rotary (const char* id)    { return *rotaries.at (id); }
LedSelector& VoicePanel::leds (const char* id)       { return *ledSelectors.at (id); }

void VoicePanel::refresh()
{
    const auto& v = proc.getDocument().getVoice (proc.getSelectedSlot());
    for (auto& [id, k] : knobs)      k->setValue (getParam (v, def (id.c_str())));
    for (auto& [id, f] : faders)     f->setValue (getParam (v, def (id.c_str())));
    for (auto& [id, s] : selectors)  s->setValue (getParam (v, def (id.c_str())));
    for (auto& [id, r] : rotaries)   r->setValue (getParam (v, def (id.c_str())));

    // Function, Glide and filter Type: the flag (values 4-6) is shown on its own button
    const auto low = [] (int x) { return x >= 4 ? x - 4 : x; };
    for (auto& [id, l] : ledSelectors)
    {
        const int raw = getParam (v, def (id.c_str()));
        l->setValue (id == "arpDir" || id == "arpMode" ? raw : low (raw));
    }
    hold.setLit (getParam (v, def ("function")) >= 4);
    glideFix.setLit (getParam (v, def ("glideType")) >= 4);
    noTrack.setLit (getParam (v, def ("filterType")) >= 4);
    octaveLeds.setValue (getParam (v, def ("octaveSet")));
    lfoToOsc1.setLit (getParam (v, def ("lfoToOsc1")) == 0);   // 0 = On
    lfoSync.setLit (getParam (v, def ("lfoSync")) == 1);
    refreshClock();
}

std::pair<int, int> VoicePanel::extent (juce::Component& c) const
{
    if (auto* k = dynamic_cast<Knob*> (&c))                 return { k->centreLine(), k->preferredHeight() };
    // every control's name sits 37 under its centre line (as under a knob), so a row's names line up
    if (auto* r = dynamic_cast<RotarySwitch*> (&c))         return { RotarySwitch::centreY + r->getExtraTop(), RotarySwitch::height + r->getExtraTop() };
    if (auto* l = dynamic_cast<LedSelector*> (&c))          return { l->blockHeight() / 2, l->preferredHeight() };
    if (dynamic_cast<VfdNumber*> (&c) != nullptr)           return { VfdNumber::windowHeight / 2, VfdNumber::height };
    if (&c == &lfoToOsc1 || &c == &lfoSync)                 return { 14, 28 };
    if (dynamic_cast<Selector*> (&c) != nullptr)            return { 14, 46 };
    if (&c == &lfoStack)                                    return { 30, 60 };
    if (&c == &octaveLeds)                                  return { LedRow::centreY, LedRow::height };
    return { 18, 47 };                                      // LED button, LED below
}

void VoicePanel::place (juce::Component& c, int x, int centreY, int w)
{
    const auto [top, h] = extent (c);
    c.setBounds (x, centreY - top, w, h);
}

std::vector<juce::Range<int>> VoicePanel::arrange (juce::Rectangle<int> area, const std::vector<std::vector<Cell>>& rows)
{
    std::vector<juce::Range<int>> spans;
    std::vector<std::pair<int, int>> rowExtents;   // { above the centre line, below it }
    int total = 0;
    for (const auto& row : rows)
    {
        int above = 0, below = 0;
        for (const auto& cell : row)
        {
            const auto [top, h] = extent (*cell.c);
            above = juce::jmax (above, top);
            below = juce::jmax (below, h - top);
        }
        rowExtents.push_back ({ above, below });
        total += above + below;
    }
    const float vGap = (float) (area.getHeight() - total) / (float) (rows.size() + 1);
    float y = (float) area.getY();
    for (size_t r = 0; r < rows.size(); ++r)
    {
        y += vGap;
        const int centre = juce::roundToInt (y) + rowExtents[r].first;
        y += (float) (rowExtents[r].first + rowExtents[r].second);
        spans.push_back ({ centre - rowExtents[r].first, centre + rowExtents[r].second });

        constexpr int joinGap = 4;
        int width = 0, blocks = 0;
        for (const auto& cell : rows[r])
        {
            width += cell.w + (cell.join ? joinGap : 0);
            blocks += cell.join ? 0 : 1;
        }
        const float hGap = (float) (area.getWidth() - width) / (float) (blocks + 1);
        float x = (float) area.getX() + hGap;
        for (const auto& cell : rows[r])
        {
            place (*cell.c, juce::roundToInt (x), centre, cell.w);
            x += (float) cell.w + (cell.join ? (float) joinGap : hGap);
        }
    }
    return spans;
}

void VoicePanel::layout()
{
    // Two floors, each read left to right. Above, the sound: the wheels, then the audio path from the
    // oscillators through the mix to the filter. Below, what shapes and moves it: LFO, glide,
    // triggering, arpeggiator, clock, and both envelopes side by side, the amplifier's last (the end
    // of the audio path). Printed lines show where things go, as on the OSCar's panel: the audio path
    // runs along the titles' rules, the modulations through the channel between the floors.
    // Each floor has a row of titles, then two rows of controls; every control of a row sits on the
    // row's centre line, so names and value windows line up across the whole floor.
    // span: one control over both rows. top2 / bottom2: a second control beside the first in that row
    // (the first gets 'split' pixels of the width).
    struct Column { int w; juce::Component* top; juce::Component* bottom; bool span = false;
                    juce::Component* top2 = nullptr; juce::Component* bottom2 = nullptr; int split = 0; };
    struct Title { int first, last; const char* text; };
    struct Block { std::vector<Column> cols; std::vector<Title> titles; std::vector<int> breaks; };   // breaks: a line after these columns
    struct Placed { juce::Rectangle<float> area; std::vector<float> starts, ends; };

    constexpr int floorGap = 22, blockGap = 8, pad = 8;          // the gap between the floors: two lanes of lines
    constexpr int titleH = 26, topRow = 90, bottomRow = 202;   // centre lines, from the floor's top
    constexpr int faderTop = 52;                               // room above the faders for the DELAY bracket
    const int floorH = (height - floorGap) / 2;
    columns.clear();
    titles.clear();
    dividers.clear();
    separators.clear();
    routes.clear();

    auto doFloor = [&] (int y0, std::vector<Block> blocks)
    {
        std::vector<Placed> placed;
        int used = 0, nCols = 0;
        for (const auto& b : blocks)
            for (const auto& c : b.cols)
            {
                used += c.w;
                ++nCols;
            }
        const int nBlocks = (int) blocks.size();
        const float gap = (float) (width - used - nBlocks * 2 * pad - (nBlocks - 1) * blockGap) / (float) (nCols + nBlocks);
        float x = 0.0f;
        for (const auto& b : blocks)
        {
            const float bx = x;
            x += (float) pad + gap;
            std::vector<float> starts, ends;
            bool allSpan = true;
            for (const auto& c : b.cols)
            {
                starts.push_back (x);
                ends.push_back (x + (float) c.w);
                allSpan = allSpan && c.span;
                const int cx = juce::roundToInt (x + (float) c.w * 0.5f);
                auto putIn = [&] (juce::Component* comp, int centre, int left, int cellW)
                {
                    if (comp == nullptr)
                        return;
                    const auto [top, h] = extent (*comp);
                    int w = cellW;
                    if (comp == &lfoToOsc1 || comp == &lfoSync || comp == &tempo)
                        w = juce::jmin (cellW, 100);
                    comp->setBounds (left + (cellW - w) / 2, y0 + centre - top, w, h);
                };
                auto put = [&] (juce::Component* comp, int centre)
                {
                    putIn (comp, centre, juce::roundToInt (x), c.w);
                };
                auto putPair = [&] (juce::Component* first, juce::Component* second, int centre)
                {
                    if (second == nullptr)
                        return put (first, centre);
                    putIn (first, centre, juce::roundToInt (x), c.split);
                    putIn (second, centre, juce::roundToInt (x) + c.split, c.w - c.split);
                };
                if (c.span && dynamic_cast<Fader*> (c.top) != nullptr)
                {
                    // from under the title to the bottom row: its name and value line up with that row's
                    c.top->setBounds (cx - c.w / 2, y0 + faderTop, c.w, bottomRow + 72 - faderTop);
                }
                else if (c.span)
                {
                    const auto [top, h] = extent (*c.top);
                    juce::ignoreUnused (top);
                    c.top->setBounds (cx - c.w / 2, y0 + (topRow - 46 + bottomRow + 74) / 2 - h / 2, c.w, h);
                }
                else
                {
                    putPair (c.top, c.top2, topRow);
                    putPair (c.bottom, c.bottom2, bottomRow);
                }
                x += (float) c.w + gap;
            }
            x += (float) pad;
            const auto area = juce::Rectangle<float> (bx, (float) y0, x - bx, (float) floorH);
            columns.push_back (area);
            placed.push_back ({ area, starts, ends });

            for (const auto& t : b.titles)
                titles.push_back ({ juce::Rectangle<float> (starts[(size_t) t.first] - gap * 0.5f, (float) y0 + 5.0f,
                                                            ends[(size_t) t.last] - starts[(size_t) t.first] + gap, 18.0f), t.text });
            for (auto i : b.breaks)
            {
                const float lx = (ends[(size_t) i] + starts[(size_t) i + 1]) * 0.5f;
                dividers.push_back (juce::Line<float> (lx, (float) y0 + (float) titleH + 10.0f, lx, (float) (y0 + floorH) - 10.0f));
            }
            // the line between the two rows, interrupted by a control spanning both
            if (! allSpan)
            {
                const float ly = (float) y0 + (float) (topRow + 52 + bottomRow - 35) * 0.5f;
                float from = area.getX() + 10.0f;
                for (size_t i = 0; i < b.cols.size(); ++i)
                    if (b.cols[i].span)
                    {
                        if (starts[i] - 4.0f > from)
                            separators.push_back ({ juce::Rectangle<float> (from, ly, starts[i] - 4.0f - from, 0.0f), {} });
                        from = ends[i] + 4.0f;
                    }
                if (area.getRight() - 10.0f > from)
                    separators.push_back ({ juce::Rectangle<float> (from, ly, area.getRight() - 10.0f - from, 0.0f), {} });
            }

            x += (float) blockGap;
        }
        return placed;
    };

    auto* none = (juce::Component*) nullptr;
    const auto top = doFloor (0, {
        { { { 86, &knob ("bendAmount"), &rotary ("tuneSemi") }, { 86, &knob ("mwPitch"), &knob ("mwFilter") } },
          { { 0, 1, "WHEELS" } }, {} },
        { { { 112, &rotary ("wave1"), &octaveLeds }, { 112, &rotary ("wave2"), &rotary ("octaveShift") },
            { 86, &rotary ("osc2Semi"), &knob ("detune") }, { 86, &rotary ("pwmRate"), &knob ("pulseWidth") } },
          { { 0, 0, "OSC 1" }, { 1, 2, "OSC 2" }, { 3, 3, "PWM" } }, { 0, 2 } },
        { { { 86, &knob ("oscBalance"), &knob ("noiseBalance") } }, { { 0, 0, "MIX" } }, {} },
        { { { 110, &rotary ("filterType"), &knob ("filterDrive") }, { 136, &knob ("filterFreq"), none, true },
            { 86, &knob ("resonance"), &knob ("lfoFilter") }, { 86, &knob ("filterSep"), &knob ("envFilter") } },
          { { 0, 3, "FILTER" } }, {} } });

    const int y1 = floorH + floorGap;
    std::vector<Column> envelopes;
    for (auto* id : { "filtAttack", "filtDecay", "filtSustain", "filtRelease", "volAttack", "volDecay", "volSustain", "volRelease" })
        envelopes.push_back ({ 34, faders.at (id).get(), none, true });
    const auto bottom = doFloor (y1, {
        { { { 110, &rotary ("lfoWave"), &knob ("lfoRate") }, { 86, &knob ("lfoPitch"), &knob ("introDelay") },
            { 96, &lfoToOsc1, &lfoSync } },
          { { 0, 2, "LFO" } }, { 1 } },
        { { { 110, &rotary ("glideType"), &knob ("glideRate") } }, { { 0, 0, "GLIDE" } }, {} },
        // one column each: triggering; the arpeggiator (Function turns it on); the clock and the tempo
        { { { 106, &rotary ("trigger"), &knob ("gateTime") },
            { 164, &rotary ("function"), &leds ("arpDir"), false, nullptr, &leds ("arpMode"), 82 },
            { 164, &syncLeds, &tempo, false, &divisionSwitch, nullptr, 64 } },
          { { 0, 0, "TRIGGERING" }, { 1, 1, "ARP" }, { 2, 2, "CLOCK" } }, { 0, 1 } },
        { envelopes, { { 0, 3, "FILTER ENVELOPE" }, { 4, 7, "AMP ENVELOPE" } }, { 3 } } });

    // The printed lines, each from a dot (where it starts) to an arrow (where it goes).
    // The audio path, in the accent colour, joins the rules under the titles: oscillators -> mix ->
    // filter, then down the filter's right edge to the amplifier's envelope.
    const float rule = 26.0f, laneUp = (float) floorH + 7.0f, laneLow = (float) floorH + 15.0f;
    const float underRow = (float) bottomRow + 75.0f;   // just under the values of a floor's bottom row
    auto colX = [] (const Placed& b, int i) { return (b.starts[(size_t) i] + b.ends[(size_t) i]) * 0.5f; };
    auto route = [this] (std::initializer_list<juce::Point<float>> pts, bool audio, bool dot, const char* label = "")
    {
        routes.push_back ({ std::vector<juce::Point<float>> (pts), audio, dot, label });
    };
    route ({ { top[1].area.getRight() - 12.0f, rule }, { top[2].area.getX() + 12.0f, rule } }, true, false);
    route ({ { top[2].area.getRight() - 12.0f, rule }, { top[3].area.getX() + 12.0f, rule } }, true, false);
    const auto& env = bottom[3];
    {
        const float edge = top[3].area.getRight() - 5.0f;
        route ({ { top[3].area.getRight() - 12.0f, rule }, { edge, rule }, { edge, (float) y1 + rule },
                 { env.area.getRight() - 13.0f, (float) y1 + rule } }, true, false);
    }

    // The modulations, through the channel between the floors, into the amounts in the filter's
    // bottom row: the filter envelope into Env Amount (lower lane), the LFO into LFO Amount (upper
    // lane), with a branch up into the oscillators (pitch).
    const auto& lfo = bottom[0];
    const float filterEnvX = (env.starts[0] + env.ends[3]) * 0.5f, lfoX = (lfo.starts[0] + lfo.ends[2]) * 0.5f;
    const float envAmountX = colX (top[3], 3), lfoAmountX = colX (top[3], 2);
    route ({ { filterEnvX, (float) y1 + 2.0f }, { filterEnvX, laneLow }, { envAmountX, laneLow }, { envAmountX, underRow } }, false, true);
    route ({ { lfoX, (float) y1 + 2.0f }, { lfoX, laneUp }, { lfoAmountX, laneUp }, { lfoAmountX, underRow } }, false, true);
    const float pitchX = top[1].area.getCentreX();   // the pitch of both oscillators
    route ({ { pitchX, laneUp }, { pitchX, (float) floorH - 2.0f } }, false, false, "PITCH");
    junctions = { { pitchX, laneUp } };

    // DELAY: on the filter envelope, from Sustain to Release
    const float slot = 3.0f;   // a fader's slot sits a little right of its centre (its scale is on the left)
    const float sx = (env.starts[2] + env.ends[2]) * 0.5f + slot, rx = (env.starts[3] + env.ends[3]) * 0.5f + slot;
    delayBracket = { sx, (float) y1 + 36.0f, rx - sx, (float) faderTop - 38.0f };
    const float wordW = juce::GlyphArrangement::getStringWidth (Fonts::bold (9.5f).withExtraKerningFactor (0.10f), "DELAY") + 8.0f;
    delayWord = juce::Rectangle<float> (wordW, 10.0f).withCentre ({ delayBracket.getCentreX(), delayBracket.getY() + 4.0f });
    plates.invalidate();
}

void VoicePanel::resized() {}

void VoicePanel::paint (juce::Graphics& g)
{
    // plates, rules, dividers and the printed lines never move: drawn once, then copied
    plates.draw (g, getLocalBounds(), [this] (juce::Graphics& p) { paintPlates (p); });

    // the words, drawn live so they stay sharp at any window size
    g.setFont (Fonts::bold (14.5f).withExtraKerningFactor (0.10f));
    g.setColour (colours::text);
    for (const auto& t : titles)
        g.drawFittedText (t.text, t.area.toNearestInt(), juce::Justification::centred, 1, 0.8f);

    g.setFont (Fonts::bold (9.5f).withExtraKerningFactor (0.10f));
    g.setColour (colours::silk);
    for (const auto& r : routes)
        if (r.label.isNotEmpty() && ! r.points.empty())   // printed beside the arrow's head
            g.drawText (r.label, juce::Rectangle<float> (60.0f, 11.0f).withPosition (r.points.back().x + 6.0f, r.points.back().y - 2.0f),
                        juce::Justification::centredLeft, false);
    if (! delayBracket.isEmpty())
        g.drawText ("DELAY", delayWord, juce::Justification::centred, false);

    // printed legends at both ends of the big filter knob's scale
    auto& f = knob ("filterFreq");
    const auto c = f.getBounds().toFloat().getCentre().withY ((float) f.getY() + 65.0f);
    g.setFont (Fonts::bold (10.5f).withExtraKerningFactor (0.06f));
    g.drawText (lang::upper (tr ("closed")), juce::Rectangle<float> (60.0f, 12.0f).withCentre (c.translated (-48.0f, 60.0f)), juce::Justification::centred);
    g.drawText (lang::upper (tr ("open")), juce::Rectangle<float> (60.0f, 12.0f).withCentre (c.translated (48.0f, 60.0f)), juce::Justification::centred);
}

void VoicePanel::paintPlates (juce::Graphics& g)
{
    for (const auto& c : columns)
        drawInset (g, c);

    // titles over a rule, the rule over the whole block
    for (const auto& c : columns)
    {
        g.setColour (colours::accent.withAlpha (0.85f));
        g.drawLine (c.getX() + 12.0f, c.getY() + 26.0f, c.getRight() - 12.0f, c.getY() + 26.0f, 1.6f);
    }

    g.setColour (colours::silkDim.withAlpha (0.4f));
    for (const auto& d : dividers)
        g.drawLine (d, 1.0f);
    for (const auto& s : separators)
        g.drawHorizontalLine (juce::roundToInt (s.area.getY()), s.area.getX(), s.area.getRight());

    // the printed lines: square corners slightly rounded, a dot where a line starts, an arrow at its end
    for (const auto& r : routes)
    {
        if (r.points.size() < 2)
            continue;
        const auto colour = r.audio ? colours::accent : colours::silk;
        const float thick = r.audio ? 1.6f : 1.4f;
        const auto tip = r.points.back();
        const auto dir = (tip - r.points[r.points.size() - 2]);
        const float len = std::hypot (dir.x, dir.y);
        if (len < 1.0f)
            continue;
        const auto unit = dir / len;
        const float headL = 7.0f, headW = 3.5f;
        juce::Path line;
        line.startNewSubPath (r.points.front());
        for (size_t i = 1; i + 1 < r.points.size(); ++i)
            line.lineTo (r.points[i]);
        line.lineTo (tip - unit * (headL - 1.0f));   // the line stops in the arrow's head
        g.setColour (colour);
        g.strokePath (line.createPathWithRoundedCorners (5.0f), juce::PathStrokeType (thick));
        const juce::Point<float> side (-unit.y, unit.x);
        juce::Path head;
        head.addTriangle (tip, tip - unit * headL + side * headW, tip - unit * headL - side * headW);
        g.fillPath (head);
        if (r.dot)
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (r.points.front()));
    }
    g.setColour (colours::silk);
    for (const auto& j : junctions)
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (j));

    // DELAY, from Sustain to Release of the filter envelope: down to both, the arrow on Release,
    // a gap in the middle for the word
    if (! delayBracket.isEmpty())
    {
        const auto& d = delayBracket;
        const float y = delayWord.getCentreY();
        g.setColour (colours::silkDim);
        g.drawLine (d.getX(), d.getBottom(), d.getX(), y, 1.2f);
        g.drawLine (d.getX(), y, delayWord.getX(), y, 1.2f);
        g.drawLine (delayWord.getRight(), y, d.getRight(), y, 1.2f);
        g.drawArrow ({ d.getRight(), y, d.getRight(), d.getBottom() + 1.0f }, 1.2f, 6.0f, 5.0f);
    }
}

//==============================================================================
// Generator
//==============================================================================
GeneratorPanel::GeneratorPanel (OscarProcessor& p) : proc (p)
{
    const char* modeNames[] = { "Vary", "New", "Breed" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = modes.add (new LedButton (tr (modeNames[i])));
        b->onClick = [this, i] { proc.genSettings().mode = (OscarProcessor::GenMode) i; refresh(); };
        addAndMakeVisible (b);
    }
    modes[0]->setTooltip (tr ("Small to wild changes around the selected voice."));
    modes[1]->setTooltip (tr ("A new voice of the chosen family, built from ranges and sections of real OSCar voices."));
    modes[2]->setTooltip (tr ("Each section from parent A or B, then a little mutation."));

    const char* padNames[] = { "Bass", "Lead", "Pad", "Perc", "Texture" };
    for (int i = 0; i < kNumFamilies; ++i)
    {
        auto* pad = pads.add (new Pad (tr (padNames[i])));
        pad->setTooltip (tr (familyName ((Family) i)));
        pad->onClick = [this, i]
        {
            proc.genSettings().family = (Family) i;
            proc.genSettings().mode = OscarProcessor::GenMode::New;
            refresh();
        };
        addAndMakeVisible (pad);
    }

    for (int i = 0; i < kNumGenSections; ++i)
    {
        auto* b = locks.add (new LedButton (tr (sectionName ((GenSection) i))));
        b->setTooltip (tr ("Lit = locked: the generator leaves this section as it is."));
        b->onClick = [this, i]
        {
            auto& l = proc.genSettings().locks.locked[(size_t) i];
            l = ! l;
            refresh();
        };
        addAndMakeVisible (b);
    }

    for (auto* s : { &parentA, &parentB })
    {
        addAndMakeVisible (s);
        s->setTooltip (tr ("A voice of the bank used as a parent."));
    }
    parentA.onChange = [this] (int v) { proc.genSettings().parentA = v; };
    parentB.onChange = [this] (int v) { proc.genSettings().parentB = v; };

    amount.setValueText ([] (int v) { return juce::String (v) + " %"; });
    amount.setKnobSize (170);       // the generator's big knob
    amount.setHotZone (0.75f);      // above 75 %: past the ranges of real voices, printed orange
    amount.setValueBeside (true);
    amount.onChange = [this] (int v) { proc.genSettings().amount = v / 100.0; refresh(); };
    amount.setTooltip (tr ("Vary: how far from the voice (above 75 % leaves the factory ranges). Breed: how much mutation."));
    addAndMakeVisible (amount);

    generateButton.fontHeight = 20.0f;
    generateButton.onClick = [this] { proc.generate(); };
    generateButton.setTooltip (tr ("Generate into the selected voice. With Live CC the OSCar plays it at once; its memory is only written by \"Send voices to synth\"."));
    prevButton.setArrow (LedButton::Arrow::Before);
    nextButton.setArrow (LedButton::Arrow::After);
    prevButton.onClick = [this] { proc.genStep (-1); };
    nextButton.onClick = [this] { proc.genStep (1); };
    auditionButton.onClick = [this] { proc.setLiveCc (! proc.getLiveCc()); };
    auditionButton.setTooltip (tr ("Same switch as Live CC: hear each result on the OSCar straight away."));
    originalButton.onClick = [this] { proc.genBackToOriginal(); };
    keepButton.onClick = [this] { proc.genKeep(); };
    keepButton.setTooltip (tr ("Keep this result in the voice and start a new series. Send voices to store it in the OSCar."));
    for (auto* c : std::initializer_list<juce::Component*> { &generateButton, &prevButton, &nextButton, &auditionButton,
                                                             &originalButton, &keepButton })
        addAndMakeVisible (c);
}

void GeneratorPanel::refresh()
{
    const auto& s = proc.genSettings();
    for (int i = 0; i < modes.size(); ++i)
        modes[i]->setLit ((int) s.mode == i);
    for (int i = 0; i < pads.size(); ++i)
        pads[i]->setLit (s.mode == OscarProcessor::GenMode::New && (int) s.family == i);
    for (int i = 0; i < locks.size(); ++i)
        locks[i]->setLit (s.locks.locked[(size_t) i]);

    std::vector<Selector::Item> voices;
    for (int pos = 0; pos < kNumVoices; ++pos)
        voices.push_back ({ slotAtPosition (pos), voiceItemText (proc.getDocument(), slotAtPosition (pos)).trim() });
    parentA.setItems (voices);
    parentB.setItems (voices);
    parentA.setValue (s.parentA);
    parentB.setValue (s.parentB);
    for (auto* c : { &parentA, &parentB })
        c->setAlpha (s.mode == OscarProcessor::GenMode::Breed ? 1.0f : 0.45f);

    amount.setValue (juce::roundToInt (s.amount * 100.0));
    amount.setLabel (s.mode == OscarProcessor::GenMode::Vary ? tr ("Variation")
                     : s.mode == OscarProcessor::GenMode::Breed ? tr ("Mutation") : tr ("Not used by New"));
    amount.setAlpha (s.mode == OscarProcessor::GenMode::New ? 0.5f : 1.0f);

    const auto& h = proc.getGenHistory();
    const bool series = ! h.empty() && proc.getGenSlot() == proc.getSelectedSlot();
    prevButton.setEnabled (series && proc.getGenIndex() > 0);
    nextButton.setEnabled (series && proc.getGenIndex() < (int) h.size() - 1);
    originalButton.setEnabled (series && proc.getGenIndex() > 0);
    keepButton.setEnabled (series);
    keepButton.setButtonText (tr ("Keep in voice %1", { juce::String (slotLabel (proc.getSelectedSlot())) }));
    auditionButton.setLit (proc.getLiveCc());
    repaint();
}

void GeneratorPanel::resized()
{
    columns = splitColumns ({ 0, 0, (float) width, (float) height }, { 425, 425, 425 });
    groups.clear();

    // A: mode, family, parents
    {
        // each group's content sits in the middle of the room above its name
        auto inner = [] (juce::Rectangle<int> group, int contentHeight)
        {
            auto r = group.reduced (12, 10).withTrimmedBottom (22);
            return r.withSizeKeepingCentre (r.getWidth(), contentHeight);
        };
        auto c = columns[0].reduced (8.0f).toNearestInt();
        auto m = c.removeFromTop (96);
        groups.push_back ({ m.toFloat(), tr ("mode") });
        auto row = inner (m, 34);
        const int bw = (row.getWidth() - 16) / 3;
        for (int i = 0; i < modes.size(); ++i)
            modes[i]->setBounds (row.getX() + i * (bw + 8), row.getY(), bw, 34);

        c.removeFromTop (8);
        auto f = c.removeFromTop (178);
        groups.push_back ({ f.toFloat(), tr ("family (new)") });
        std::initializer_list<juce::Component*> list { pads[0], pads[1], pads[2], pads[3], pads[4] };
        spread (list, inner (f, 94), 70, 94);
        for (auto* pad : pads)
            pad->setFace (pad->getBounds());   // the same place, with room for the halo

        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("parents (breed)") });
        auto p = inner (c, 46 + 10 + 46 + 14 + 40);   // A above B, then the note
        parentA.setBounds (p.removeFromTop (46));
        p.removeFromTop (10);
        parentB.setBounds (p.removeFromTop (46));
        p.removeFromTop (14);
        parentsNote = p.toFloat();
    }

    // B, top to bottom and evenly spaced: the amount knob with its value, the note, Generate, the
    // history navigation
    {
        auto c = columns[1].reduced (8.0f).toNearestInt();
        groups.push_back ({ c.toFloat(), tr ("generate") });
        auto g = c.reduced (12, 12).withTrimmedBottom (22);
        const int heights[] = { amount.preferredHeight(), 36, 64, 34 };
        const int gap = (g.getHeight() - (heights[0] + heights[1] + heights[2] + heights[3])) / 4;
        int y = g.getY() + gap / 2;
        amount.setBounds (g.getX(), y, g.getWidth(), heights[0]);
        y += heights[0] + gap;
        amountNote = juce::Rectangle<int> (g.getX(), y, g.getWidth(), heights[1]).toFloat();
        y += heights[1] + gap;
        generateButton.setFace ({ g.getCentreX() - 130, y, 260, heights[2] });
        y += heights[2] + gap;
        auto row = juce::Rectangle<int> (g.getX(), y, g.getWidth(), heights[3]);
        prevButton.setBounds (row.removeFromLeft (118));
        row.removeFromLeft (8);
        nextButton.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (8);
        auditionButton.setBounds (row);
    }

    // C: locks, history
    {
        auto c = columns[2].reduced (8.0f).toNearestInt();
        auto l = c.removeFromTop (226);
        groups.push_back ({ l.toFloat(), tr ("locks") });
        auto g = l.reduced (10, 10);
        const int bw = (g.getWidth() - 8) / 2;
        for (int i = 0; i < locks.size(); ++i)
            locks[i]->setBounds (g.getX() + (i % 2) * (bw + 8), g.getY() + (i / 2) * 38, bw, 32);
        locksNote = juce::Rectangle<int> (g.getX(), g.getY() + 152, g.getWidth(), 36).toFloat();

        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("history") });
        auto h = c.reduced (10, 10).withTrimmedBottom (22);
        auto buttons = h.removeFromBottom (36);
        originalButton.setBounds (buttons.removeFromLeft ((buttons.getWidth() - 8) / 2));
        buttons.removeFromLeft (8);
        keepButton.setFace (buttons);
        h.removeFromBottom (8);
        historyArea = h.toFloat();
    }
    addAndMakeVisible (wear);
    setupWear (*this, wear, columns, { historyArea },
               { { generateButton.getBounds().toFloat().getCentre(), 90.0f }, { amount.getBounds().toFloat().getCentre().translated (0.0f, -20.0f), 80.0f } });
}

void GeneratorPanel::paint (juce::Graphics& g)
{
    for (const auto& c : columns)
        drawInset (g, c);
    for (const auto& [area, label] : groups)
        drawGroup (g, area, label);

    // amount scale words, either side of the knob near its foot
    const auto knob = amount.getBounds().withSizeKeepingCentre (amount.knobDiameter(), amount.getHeight()).withHeight (amount.knobDiameter());
    g.setFont (Fonts::bold (12.0f).withExtraKerningFactor (0.1f));
    g.setColour (colours::label);
    g.drawText (tr ("SUBTLE"), juce::Rectangle<int> (knob.getX() - 96, knob.getBottom() - 26, 90, 14), juce::Justification::centredRight);
    g.setColour (colours::vfd);
    g.drawText (tr ("WILD"), juce::Rectangle<int> (knob.getRight() + 6, knob.getBottom() - 26, 90, 14), juce::Justification::centredLeft);

    const auto mode = proc.genSettings().mode;
    drawText (g, amountNote, mode == OscarProcessor::GenMode::New
                  ? tr ("New builds a whole voice of the chosen family from real OSCar voices, then checks it can be heard.")
                  : tr ("Above 75 % the generator leaves the ranges used by real voices. Locked sections never move."),
              13.0f, colours::dim, juce::Justification::centredTop);
    drawText (g, parentsNote, tr ("Breed takes each section from parent A or parent B, then mutates a little (Mutation knob). The result goes into the selected voice."), 13.0f);
    drawText (g, locksNote, tr ("Tune, wheels, arpeggiator, function and gate time are never changed."), 13.0f);

    // history (newest first)
    drawVfdWindow (g, historyArea);
    const auto& h = proc.getGenHistory();
    const bool series = ! h.empty() && proc.getGenSlot() == proc.getSelectedSlot();
    auto list = historyArea.reduced (6.0f);
    g.setFont (Fonts::mono (14.0f));
    if (! series)
    {
        g.setColour (colours::vfdDim);
        g.drawFittedText (tr ("Press GENERATE: results are listed here, and you can go back to any of them."),
                          list.reduced (4.0f).toNearestInt(), juce::Justification::centred, 3);
        return;
    }
    const float rowH = 22.0f;
    const int visible = juce::jmax (1, (int) (list.getHeight() / rowH));
    const int newest = (int) h.size() - 1;
    int row = 0;
    for (int i = newest; i >= 0 && row < visible; --i, ++row)
    {
        auto r = list.removeFromTop (rowH);
        const bool current = i == proc.getGenIndex();
        if (current)
        {
            g.setColour (colours::vfd);
            g.fillRoundedRectangle (r, 2.0f);
        }
        g.setColour (current ? juce::Colour (0xff1e0b03) : colours::vfd);
        g.drawText (h[(size_t) i].label, r.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
        g.drawText (h[(size_t) i].when.formatted ("%H:%M:%S"), r.reduced (8.0f, 0.0f), juce::Justification::centredRight);
    }
}

//==============================================================================
// Banks
//==============================================================================
BanksPanel::BanksPanel (OscarProcessor& p, OscarEditor& e) : proc (p), editor (e)
{
    for (auto* l : { &bankList, &libraryList })
    {
        l->setRowHeight (22);
        l->setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        l->setOutlineThickness (0);
        addAndMakeVisible (l);
    }

    renameButton.onClick = [this] { editor.renameSelected(); };
    copyButton.onClick = [this]
    {
        proc.getDocument().copySlot (proc.getSelectedSlot());
        proc.addLog (tr ("Copied voice %1.", { juce::String (slotLabel (proc.getSelectedSlot())) }));
    };
    pasteButton.onClick = [this] { proc.getDocument().pasteSlot (proc.getSelectedSlot()); };
    swapButton.onClick = [this] { editor.swapSelected (swapButton); };
    initButton.onClick = [this] { editor.initSelected(); };
    initButton.setTooltip (tr ("A plain starting sound: oscillator 1 alone on sawtooth, filter wide open, no modulation. Undo brings the voice back."));
    revertButton.onClick = [this] { proc.getDocument().revertSlot (proc.getSelectedSlot()); };
    revertButton.setTooltip (tr ("Back to what the OSCar holds for this voice"));
    selectButton.onClick = [this]
    {
        const auto err = proc.selectOnOscar (proc.getSelectedSlot());
        if (err.isNotEmpty()) proc.addLog (err);
    };
    selectButton.setTooltip (tr ("Program change: the OSCar plays this voice (program change must be on: SPACE + key -9)."));
    exportVoiceButton.onClick = [this] { editor.exportSelectedVoice(); };
    importVoiceButton.onClick = [this] { editor.importIntoSelectedVoice(); };

    allButton.onClick = [this] { filter = 0; rescanLibrary(); };
    banksButton.onClick = [this] { filter = 1; rescanLibrary(); };
    voicesButton.onClick = [this] { filter = 2; rescanLibrary(); };
    backupsButton.onClick = [this] { filter = 3; rescanLibrary(); };

    loadButton.onClick = [this] { loadSelectedEntry(); };
    loadButton.setTooltip (tr ("Loads the bank into the editor. The OSCar is only changed by \"Send voices to synth\"."));
    saveAsButton.onClick = [this] { saveBankAs(); };
    openFileButton.onClick = [this] { editor.loadBankFromFile(); };
    exportSyxButton.onClick = [this] { editor.exportBankSyx(); };
    factoryButton.onClick = [this]
    {
        proc.getDocument().loadFactoryVoices();
        proc.addLog (tr ("Factory voices loaded into the editor (not yet sent to the OSCar). Undo to go back."));
    };
    folderButton.onClick = []
    {
        auto dir = OscarProcessor::getUserFolder();
        dir.createDirectory();
        dir.startAsProcess();
    };
    sequencesButton.onClick = [this] { editor.confirmThenSendSequences(); };

    for (auto* b : std::initializer_list<juce::Component*> { &renameButton, &copyButton, &pasteButton, &swapButton, &initButton, &revertButton,
                                                             &selectButton, &exportVoiceButton, &importVoiceButton, &allButton,
                                                             &banksButton, &voicesButton, &backupsButton, &loadButton, &saveAsButton,
                                                             &openFileButton, &exportSyxButton, &factoryButton, &folderButton,
                                                             &sequencesButton })
        addAndMakeVisible (b);

    rescanLibrary();
}

BanksPanel::~BanksPanel()
{
    bankList.setModel (nullptr);
    libraryList.setModel (nullptr);
}

void BanksPanel::refresh()
{
    const juce::ScopedValueSetter<bool> s (updating, true);
    bankList.updateContent();
    const int slot = proc.getSelectedSlot();
    const int row = positionOfSlot (slot);   // rows in the keyboard's order: -12 .. -1, 1 .. 24
    if (bankList.getSelectedRow() != row)
    {
        bankList.selectRow (row);
        bankList.scrollToEnsureRowIsOnscreen (row);
    }
    bankList.repaint();

    const auto& doc = proc.getDocument();
    pasteButton.setEnabled (doc.canPaste());
    revertButton.setEnabled (doc.isSlotEdited (slot));
    const auto& seq = doc.getSequences();
    sequencesButton.setEnabled (seq.has_value() && ! seq->empty());
    allButton.setLit (filter == 0);
    banksButton.setLit (filter == 1);
    voicesButton.setLit (filter == 2);
    backupsButton.setLit (filter == 3);
}

void BanksPanel::rescanLibrary()
{
    entries.clear();
    for (const auto& f : OscarProcessor::getBanksFolder().findChildFiles (juce::File::findFiles, false, "*.oscbank;*.syx"))
        entries.push_back ({ f, "BANK", f.getLastModificationTime() });
    for (const auto& f : OscarProcessor::getVoicesFolder().findChildFiles (juce::File::findFiles, false, "*.oscvoice"))
        entries.push_back ({ f, "VOICE", f.getLastModificationTime() });

    for (const auto& f : OscarProcessor::getBackupFolder().findChildFiles (juce::File::findFiles, false, "*.syx"))
    {
        const auto n = f.getFileNameWithoutExtension().toLowerCase();
        juce::String kind = "BACKUP";
        if (n.contains ("before send"))    kind = "BEFORE SEND";
        else if (n.contains ("received"))  kind = "FROM OSCAR";
        else if (n.contains ("sent"))      kind = "SENT";
        entries.push_back ({ f, kind, f.getLastModificationTime() });
    }
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.when > b.when; });

    shown.clear();
    for (const auto& e : entries)
    {
        const bool backup = e.kind != "BANK" && e.kind != "VOICE";
        if (filter == 0 || (filter == 1 && e.kind == "BANK") || (filter == 2 && e.kind == "VOICE") || (filter == 3 && backup))
            shown.push_back (e);
    }

    libraryList.updateContent();
    libraryList.repaint();
    refresh();
    bankList.scrollToEnsureRowIsOnscreen (positionOfSlot (proc.getSelectedSlot()));
}

int BanksPanel::getNumRows()
{
    return kNumVoices;
}

void BanksPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& doc = proc.getDocument();
    const bool firstPositive = row == 12;   // the line between -1 and 1
    row = slotAtPosition (row);
    if (row < 0)
        return;
    if (selected)
    {
        g.setColour (colours::copper.withAlpha (0.85f));
        g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 2.0f);
    }
    else if (firstPositive)
    {
        g.setColour (colours::vfd.withAlpha (0.25f));
        g.drawHorizontalLine (0, 4.0f, (float) w - 4.0f);
    }
    const auto ink = selected ? juce::Colour (0xff1e0b03) : colours::vfd;
    g.setFont (Fonts::mono (15.0f));
    g.setColour (ink);
    g.drawText (juce::String (slotLabel (row)), 6, 0, 34, h, juce::Justification::centredRight);
    g.setColour (doc.getName (row).isEmpty() && ! selected ? colours::vfdDim : ink);
    g.drawText (doc.getDisplayName (row), 50, 0, w - 90, h, juce::Justification::centredLeft, true);
    if (doc.hasUnsentEdits (row))
    {
        g.setFont (Fonts::mono (12.0f));
        g.setColour (selected ? ink : colours::warn);
        g.drawText (tr ("EDITED"), w - 84, 0, 78, h, juce::Justification::centredRight);
    }
}

void BanksPanel::selectedRowsChanged (int row)
{
    const int slot = slotAtPosition (row);
    if (updating || slot < 0 || slot == proc.getSelectedSlot())
        return;
    proc.chooseVoice (slot);
}

void BanksPanel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    editor.renameSelected();
}

int BanksPanel::LibraryModel::getNumRows()
{
    return (int) owner.shown.size();
}

void BanksPanel::LibraryModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) owner.shown.size())
        return;
    const auto& e = owner.shown[(size_t) row];
    if (selected)
    {
        g.setColour (colours::copper.withAlpha (0.85f));
        g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 2.0f);
    }
    const auto ink = selected ? juce::Colour (0xff1e0b03) : colours::vfd;
    g.setFont (Fonts::mono (14.0f));
    g.setColour (selected ? ink : colours::vfdDim);
    g.drawText (e.when.formatted ("%Y-%m-%d %H:%M"), 8, 0, 140, h, juce::Justification::centredLeft);
    g.setColour (ink);
    g.drawText (tr (e.kind), 150, 0, 110, h, juce::Justification::centredLeft);
    juce::String name = e.file.getFileNameWithoutExtension();
    if (e.kind == "BANK" && e.file.hasFileExtension ("syx"))
        name += "  (.syx)";
    else if (e.kind != "BANK" && e.kind != "VOICE")
        name = name.substring (20);   // drop the date already shown
    g.drawText (name, 262, 0, w - 270, h, juce::Justification::centredLeft, true);
}

void BanksPanel::LibraryModel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    owner.loadSelectedEntry();
}

void BanksPanel::loadSelectedEntry()
{
    const int row = libraryList.getSelectedRow();
    if (row < 0 || row >= (int) shown.size())
    {
        proc.addLog (tr ("Choose a bank, a voice or a backup in the list first."));
        return;
    }
    const auto& entry = shown[(size_t) row];
    if (entry.kind == "VOICE")   // a voice goes into the selected voice
    {
        const int slot = proc.getSelectedSlot();
        editor.showResult (proc.getDocument().importVoice (slot, entry.file),
                           tr ("Imported %1 into voice %2 (not yet sent to the OSCar).",
                               { entry.file.getFileName(), juce::String (slotLabel (slot)) }));
        return;
    }
    juce::String report;
    const auto r = proc.getDocument().loadFile (shown[(size_t) row].file, report);
    editor.showResult (r);
    if (r.wasOk())
        proc.addLog (report + " " + tr ("Send voices to synth to put it in the OSCar."));
}

void BanksPanel::saveBankAs()
{
    editor.askText (tr ("Save the bank"), tr ("Name of the bank (saved in Documents > OXIde > Banks, with the voice names)."),
                    tr ("My bank"), tr ("Save"), [this] (const juce::String& raw)
    {
        auto name = juce::File::createLegalFileName (raw.trim());
        if (name.isEmpty())
            return;
        auto dir = OscarProcessor::getBanksFolder();
        dir.createDirectory();
        auto file = dir.getChildFile (name + ".oscbank");
        if (file.exists())
            file = dir.getNonexistentChildFile (name, ".oscbank", false);
        editor.showResult (proc.getDocument().saveBankFile (file), tr ("Saved %1.", { file.getFileName() }));
        rescanLibrary();
    });
}

void BanksPanel::resized()
{
    const auto cols = splitColumns ({ 0, 0, (float) width, (float) height }, { 520, 776 });
    groups.clear();
    constexpr int buttonRows = 3 * 34 + 2 * 8;   // both columns: three rows of buttons

    {
        auto c = cols[0].reduced (8.0f).toNearestInt();
        groups.push_back ({ c.toFloat(), tr ("bank in the editor") });
        auto g = c.reduced (12, 12).withTrimmedBottom (22);
        auto buttons = g.removeFromBottom (buttonRows);
        g.removeFromBottom (8);
        bankList.setBounds (g.reduced (4));
        // from the top (-12) when the selected voice fits on the first screen, else just enough to show it
        bankList.setVerticalPosition (0.0);
        bankList.scrollToEnsureRowIsOnscreen (positionOfSlot (proc.getSelectedSlot()));
        // three rows of three: clipboard, what replaces the voice, the voice on the synth / in a file
        const int bw = (buttons.getWidth() - 16) / 3;
        juce::Component* grid[3][3] = { { &renameButton, &copyButton, &pasteButton },
                                        { &swapButton, &initButton, &revertButton },
                                        { &selectButton, &exportVoiceButton, &importVoiceButton } };
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 3; ++i)
                grid[r][i]->setBounds (buttons.getX() + i * (bw + 8), buttons.getY() + r * 42, bw, 34);
        groups.push_back ({ bankList.getBounds().toFloat().expanded (4.0f), {} });
    }

    {
        auto c = cols[1].reduced (8.0f).toNearestInt();
        groups.push_back ({ c.toFloat(), tr ("library: your banks, your voices and the automatic backups") });
        auto g = c.reduced (12, 12).withTrimmedBottom (22);
        auto top = g.removeFromTop (32);
        allButton.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (8);
        banksButton.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (8);
        voicesButton.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        backupsButton.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (16);
        libraryNote = top.toFloat();

        auto buttons = g.removeFromBottom (buttonRows);   // same height as the bank's, so both lists end level
        g.removeFromBottom (8);
        g.removeFromTop (8);
        libraryList.setBounds (g.reduced (4));
        loadButton.setFace ({ buttons.getX(), buttons.getY(), 220, buttons.getHeight() - 2 });
        auto rest = buttons.withTrimmedLeft (232);
        const int bw = (rest.getWidth() - 8) / 2;
        juce::Component* grid[3][2] = { { &saveAsButton, &openFileButton },
                                        { &exportSyxButton, &sequencesButton },
                                        { &factoryButton, &folderButton } };
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 2; ++i)
                grid[r][i]->setBounds (rest.getX() + i * (bw + 8), rest.getY() + r * 42, bw, 34);
        groups.push_back ({ libraryList.getBounds().toFloat().expanded (4.0f), {} });
    }
    std::vector<juce::Rectangle<float>> windows;
    for (const auto& [area, label] : groups)
        if (label.isEmpty())
            windows.push_back (area);
    addAndMakeVisible (wear);
    setupWear (*this, wear, {}, windows, {});
}

void BanksPanel::paint (juce::Graphics& g)
{
    for (const auto& [area, label] : groups)
    {
        if (label.isEmpty())
            drawVfdWindow (g, area);
        else
            drawGroup (g, area, label);
    }
    drawText (g, libraryNote, tr ("Double-click: a bank or a backup replaces the bank in the editor, a voice goes into the selected voice."),
              12.5f, colours::dim, juce::Justification::centredLeft);
    if (shown.empty())
    {
        g.setFont (Fonts::mono (14.0f));
        g.setColour (colours::vfdDim);
        g.drawFittedText (tr ("Nothing here yet: \"Save bank as...\" adds your banks, \"Save voice...\" your voices; dumps and sends add backups."),
                          libraryList.getBounds().reduced (20), juce::Justification::centred, 3);
    }
}

//==============================================================================
// Morph
//==============================================================================
namespace
{
    // the 26 knobs of a voice, by section, for the preview
    struct PreviewSection { const char* name; std::vector<const char*> ids; };
    const std::vector<PreviewSection>& previewSections()
    {
        static const std::vector<PreviewSection> s = {
            { "oscillators", { "oscBalance", "noiseBalance", "pulseWidth", "detune" } },
            { "filter",      { "filterFreq", "resonance", "filterSep", "envFilter", "filterDrive" } },
            { "filter env",  { "filtAttack", "filtDecay", "filtSustain", "filtRelease" } },
            { "amp env",     { "volAttack", "volDecay", "volSustain", "volRelease" } },
            { "modulation",  { "lfoRate", "introDelay", "lfoPitch", "lfoFilter" } },
            { "keyboard",    { "bendAmount", "mwPitch", "mwFilter" } },
            { "performance", { "glideRate", "gateTime" } } };
        return s;
    }
}

MorphPanel::MorphPanel (OscarProcessor& p) : proc (p)
{
    voiceA.setTooltip (tr ("Voice A: the sound at the left end of the fader."));
    voiceB.setTooltip (tr ("Voice B: the sound at the right end of the fader."));
    // a voice chosen here waits for Load; the blend keeps the loaded A and B until then
    const auto choose = [this] (bool b, int v)
    {
        const int loaded = b ? proc.morphSettings().slotB : proc.morphSettings().slotA;
        (b ? pendingB : pendingA) = v == loaded ? -1 : v;
        (b ? voiceB : voiceA).setValue (v);
        refreshLoad();
    };
    voiceA.onChange = [choose] (int v) { choose (false, v); };
    voiceB.onChange = [choose] (int v) { choose (true, v); };
    takeA.onClick = [this, choose] { choose (false, proc.getSelectedSlot()); };
    takeB.onClick = [this, choose] { choose (true, proc.getSelectedSlot()); };
    loadA.onClick = [this] { if (pendingA >= 0) { proc.loadMorphSource (false, pendingA); pendingA = -1; refresh(); } };
    loadB.onClick = [this] { if (pendingB >= 0) { proc.loadMorphSource (true, pendingB); pendingB = -1; refresh(); } };
    for (auto* s : { &voiceA, &voiceB })
        addAndMakeVisible (s);
    for (auto* b : { &takeA, &takeB })
    {
        b->setTooltip (tr ("Choose the voice selected in the editor (then Load)."));
        addAndMakeVisible (b);
    }
    loadA.setTooltip (tr ("Load the chosen voice into A. Until then the blend keeps the voice A already loaded; during a blend, the sound glides to the new pair."));
    loadB.setTooltip (tr ("Load the chosen voice into B. Until then the blend keeps the voice B already loaded; during a blend, the sound glides to the new pair."));
    for (auto* b : { &loadA, &loadB })
        addAndMakeVisible (b);

    fader.setSliderStyle (juce::Slider::LinearHorizontal);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    fader.getProperties().set ("crossfader", true);
    fader.setDoubleClickReturnValue (true, 0.5);
    fader.setTooltip (tr ("Crossfader: A on the left, B on the right. Double-click: half way. Can be automated from the DAW (parameter Morph A-B)."));
    faderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getHostParameters(), "morph", fader);
    addAndMakeVisible (fader);

    smoothing.setValue (juce::roundToInt (proc.morphSettings().smoothing * 100.0f));
    smoothing.onChange = [this] (int v) { proc.setMorphSmoothing ((float) v / 100.0f); };
    smoothing.setValueText ([] (int v) { return juce::String (v * 10); });   // milliseconds
    smoothing.setScale ("0", "2s");
    smoothing.setTooltip (tr ("Smoothing, in milliseconds: the shortest time the blend takes from A to B, whatever the fader does. A sudden move becomes a glide, and the CC sent to the OSCar stay spaced out."));
    addAndMakeVisible (smoothing);

    keepButton.onClick = [this] { proc.morphKeep(); };
    keepButton.setTooltip (tr ("Keep the blend in the selected voice. The next move starts again from voices A and B."));
    addAndMakeVisible (keepButton);

    addAndMakeVisible (wear);
    startTimerHz (30);
}

MorphPanel::~MorphPanel()
{
    stopTimer();
    faderAttachment.reset();
}

void MorphPanel::refreshLoad()
{
    for (auto [button, pending] : { std::pair<LedButton*, int> { &loadA, pendingA }, { &loadB, pendingB } })
    {
        button->setEnabled (pending >= 0);
        button->setLit (pending >= 0);
    }
}

void MorphPanel::refresh()
{
    const auto& doc = proc.getDocument();
    std::vector<Selector::Item> items;
    for (int pos = 0; pos < kNumVoices; ++pos)
    {
        const int i = slotAtPosition (pos);
        items.push_back ({ i, juce::String (slotLabel (i)) + "  " + doc.getDisplayName (i) });
    }
    voiceA.setItems (items);
    voiceB.setItems (items);
    voiceA.setValue (pendingA >= 0 ? pendingA : proc.morphSettings().slotA);
    voiceB.setValue (pendingB >= 0 ? pendingB : proc.morphSettings().slotB);
    refreshLoad();
    // only what shows the voices: the fader moves the voice up to 30 times a second
    for (const auto& r : { preview, summaryA, summaryB, info })
        repaint (r.toNearestInt().expanded (2));
}

void MorphPanel::timerCallback()
{
    const float pos = proc.getMorphPosition();
    if (pos != shownPosition)
    {
        shownPosition = pos;
        repaint (readout.toNearestInt().expanded (4));
    }
}

void MorphPanel::resized()
{
    columns = splitColumns ({ 0, 0, (float) width, (float) height }, { 290, 700, 290 });
    groups.clear();
    auto side = [this] (int i, Selector& sel, LedButton& take, LedButton& load, juce::Rectangle<float>& big, juce::Rectangle<float>& summary, const juce::String& label)
    {
        auto c = columns[(size_t) i].reduced (8.0f).toNearestInt();
        groups.push_back ({ c.toFloat(), label });
        auto g = c.reduced (16, 14).withTrimmedBottom (22);
        big = g.removeFromTop (112).toFloat();
        sel.setBounds (g.removeFromTop (46));
        g.removeFromTop (12);
        auto row = g.removeFromTop (34);   // choose the editor's voice | load it
        take.setBounds (row.removeFromLeft ((row.getWidth() - 8) / 2));
        row.removeFromLeft (8);
        load.setBounds (row);
        g.removeFromTop (16);
        summary = g.toFloat();
    };
    side (0, voiceA, takeA, loadA, bigA, summaryA, tr ("voice a"));
    side (2, voiceB, takeB, loadB, bigB, summaryB, tr ("voice b"));

    auto c = columns[1].reduced (8.0f).toNearestInt();
    groups.push_back ({ c.toFloat(), tr ("morph") });
    auto g = c.reduced (22, 14).withTrimmedBottom (22);
    // the preview takes what the fader row and the bottom row leave
    preview = g.removeFromTop (g.getHeight() - 12 - 104 - 16 - Knob::height (KnobStyle::Chrome)).toFloat();
    g.removeFromTop (12);
    auto row = g.removeFromTop (104);
    readout = row.removeFromTop (30).withSizeKeepingCentre (64, 28).toFloat();
    row.removeFromTop (4);
    letterA = row.removeFromLeft (44).toFloat();
    letterB = row.removeFromRight (44).toFloat();
    fader.setBounds (row.reduced (10, 0).withSizeKeepingCentre (row.getWidth() - 20, 70));
    g.removeFromTop (16);
    auto bottom = g;
    smoothing.setBounds (bottom.removeFromLeft (96).withHeight (Knob::height (KnobStyle::Chrome)));
    keepButton.setFace (bottom.removeFromRight (210).withSizeKeepingCentre (210, 54));
    info = bottom.reduced (18, 4).toFloat();

    setupWear (*this, wear, columns, { preview, readout, summaryA, summaryB }, { { fader.getBounds().toFloat().getCentre(), 150.0f } });
}

void MorphPanel::paint (juce::Graphics& g)
{
    for (const auto& c : columns)
        drawInset (g, c);
    for (const auto& [area, label] : groups)
        drawGroup (g, area, label);

    g.setFont (Fonts::title (104.0f));
    g.setColour (colours::label.withAlpha (0.8f));
    g.drawText ("A", bigA, juce::Justification::centred);
    g.drawText ("B", bigB, juce::Justification::centred);
    g.setFont (Fonts::title (34.0f));
    g.setColour (colours::silk);
    g.drawText ("A", letterA, juce::Justification::centred);
    g.drawText ("B", letterB, juce::Justification::centred);
    drawSegmentReadout (g, readout, juce::String (juce::roundToInt (proc.getMorphPosition() * 100.0f)), 3, true);

    // preview: the 26 knobs of the selected voice, with a mark where A and B are
    drawVfdWindow (g, preview);
    const auto& doc = proc.getDocument();
    const auto& v = doc.getVoice (proc.getSelectedSlot());
    const auto& a = doc.getVoice (proc.morphSettings().slotA);
    const auto& b = doc.getVoice (proc.morphSettings().slotB);
    auto inner = preview.reduced (16.0f, 12.0f);
    auto labels = inner.removeFromBottom (16.0f);
    inner.removeFromBottom (6.0f);
    const float gap = 14.0f, step = (inner.getWidth() - gap * (float) (previewSections().size() - 1)) / 26.0f;
    float x = inner.getX();
    for (const auto& section : previewSections())
    {
        const float x0 = x;
        for (auto* id : section.ids)
        {
            const auto& d = *findParam (id);
            auto level = [&d] (const VoiceData& voice) { return (float) getParam (voice, d) / (float) kKnobMaxEditable; };
            auto col = juce::Rectangle<float> (x, inner.getY(), step, inner.getHeight()).reduced (step * 0.24f, 0.0f);
            g.setColour (colours::vfd.withAlpha (0.08f));
            g.fillRect (col);
            g.setColour (colours::vfd);
            g.fillRect (col.withTop (col.getBottom() - col.getHeight() * level (v)));
            g.setColour (colours::vfdDim);                                                     // A: a short dash on the left
            g.fillRect (col.getX() - 3.0f, col.getBottom() - col.getHeight() * level (a) - 1.0f, col.getWidth() * 0.5f + 3.0f, 2.0f);
            g.setColour (juce::Colour (0xfff3e2d4));                                          // B: on the right
            g.fillRect (col.getCentreX(), col.getBottom() - col.getHeight() * level (b) - 1.0f, col.getWidth() * 0.5f + 3.0f, 2.0f);
            x += step;
        }
        g.setFont (Fonts::mono (11.0f));
        g.setColour (colours::vfdDim);
        g.drawFittedText (lang::upper (tr (section.name)), juce::Rectangle<float> (x0, labels.getY(), x - x0, 16.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.6f);
        x += gap;
    }

    // the switches of A and B: they change half way
    for (const auto& [area, voice, slotOf] : { std::tuple<juce::Rectangle<float>, const VoiceData*, int> { summaryA, &a, proc.morphSettings().slotA },
                                                { summaryB, &b, proc.morphSettings().slotB } })
    {
        drawVfdWindow (g, area);
        auto lines = area.reduced (12.0f, 10.0f);
        const float lineH = lines.getHeight() / 7.0f;
        {   // the voice loaded at this end
            auto line = lines.removeFromTop (lineH);
            g.setFont (Fonts::mono (13.0f));
            g.setColour (colours::vfdDim);
            g.drawText (lang::upper (tr ("Loaded")), line, juce::Justification::centredLeft);
            g.setColour (colours::vfd.brighter (0.2f));
            g.drawFittedText (juce::String (slotLabel (slotOf)) + " " + lang::upper (doc.getDisplayName (slotOf)),
                              line.withTrimmedLeft (70.0f).toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
        }
        for (auto* id : { "wave1", "wave2", "filterType", "trigger", "function", "lfoWave" })
        {
            const auto& d = *findParam (id);
            const int value = getParam (*voice, d);
            juce::String name = juce::String (value);
            for (const auto& c : d.choices)
                if (c.value == value)
                    name = c.label;
            auto line = lines.removeFromTop (lineH);
            g.setFont (Fonts::mono (13.0f));
            g.setColour (colours::vfdDim);
            g.drawText (lang::upper (d.name), line, juce::Justification::centredLeft);
            g.setColour (colours::vfd);
            g.drawText (lang::upper (name), line, juce::Justification::centredRight);
        }
    }

    const int slot = proc.getSelectedSlot();
    drawText (g, info, tr ("Voice %1 follows the fader: knobs glide from A to B, switches change half way. A and B only change when you Load a voice. With Live CC on, the OSCar follows live. Undo brings the voice back. Dashes: where A (left) and B (right) are.",
                           { juce::String (slotLabel (slot)) + " " + doc.getDisplayName (slot) }), 13.0f, colours::dim, juce::Justification::centredLeft);
}

//==============================================================================
// Waveforms
//==============================================================================
WavesPanel::WavesPanel (OscarProcessor& p, OscarEditor& e) : proc (p), editor (e)
{
    for (int t = 0; t < kNumWaveTables; ++t)
    {
        auto* b = tables.add (new LedButton (tr ("Wave %1", { juce::String (-8 - t) })));
        b->onClick = [this, t] { table = t; refresh(); };
        addAndMakeVisible (b);
    }
    sendButton.onClick = [this] { editor.confirmThenSendWaves(); };
    addAndMakeVisible (sendButton);
}

void WavesPanel::refresh()
{
    const auto& doc = proc.getDocument();
    const bool has = doc.getWaves().has_value();
    sendButton.setEnabled (has);
    for (int i = 0; i < tables.size(); ++i)
    {
        tables[i]->setEnabled (has);
        tables[i]->setLit (i == table);
    }
    if (! has)
        info = tr ("No waveform data yet. On the OSCar: hold INSERT, press a WAVEFORM button, then SPACE.");
    else if (doc.areWavesEdited())
        info = tr ("Edited, not sent. 24 harmonic levels per user waveform (raw 0-255, to be confirmed on hardware). Drag to draw.");
    else
        info = tr ("Same as in the OSCar. 24 harmonic levels per user waveform (raw 0-255, to be confirmed on hardware). Drag to draw.");
    repaint();
}

void WavesPanel::resized()
{
    group = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (8.0f);
    auto g = group.reduced (14.0f, 12.0f).withTrimmedBottom (22.0f).toNearestInt();
    auto top = g.removeFromTop (34);
    for (auto* b : tables)
    {
        b->setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
    }
    sendButton.setFace (top.removeFromRight (230).withSizeKeepingCentre (230, 40));
    g.removeFromTop (8);
    infoArea = g.removeFromTop (22).toFloat();
    g.removeFromTop (8);
    auto area = g.toFloat();
    scope = area.removeFromRight (300.0f).withTrimmedTop (22.0f).withTrimmedBottom (40.0f);
    area.removeFromRight (24.0f);
    bars = area.withTrimmedLeft (44.0f).withTrimmedTop (22.0f).withTrimmedBottom (26.0f);
    addAndMakeVisible (wear);
    setupWear (*this, wear, { juce::Rectangle<float> (0, 0, (float) width, (float) height) },
               { bars.withTrimmedTop (-22.0f), scope },
               { { bars.getCentre(), 160.0f } });
}

void WavesPanel::paint (juce::Graphics& g)
{
    drawInset (g, { 0, 0, (float) width, (float) height });
    drawGroup (g, group, tr ("harmonics"));
    drawText (g, infoArea, info, 13.0f, colours::dim, juce::Justification::centredLeft);

    const auto& waves = proc.getDocument().getWaves();
    const float step = bars.getWidth() / (float) kNumHarmonics, slotW = 16.0f;

    // printed scale on the left
    g.setColour (colours::silkDim);
    for (int i = 0; i <= 10; ++i)
    {
        const float y = bars.getBottom() - bars.getHeight() * (float) i / 10.0f;
        const bool major = i % 5 == 0;
        g.fillRect (bars.getX() - (major ? 14.0f : 10.0f), y - 0.5f, major ? 9.0f : 5.0f, 1.0f);
    }
    g.setFont (Fonts::bold (10.0f));
    g.setColour (colours::silk);
    g.drawText ("255", juce::Rectangle<float> (bars.getX() - 46.0f, bars.getY() - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
    g.drawText ("0", juce::Rectangle<float> (bars.getX() - 46.0f, bars.getBottom() - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);

    std::array<float, kNumHarmonics> levels {};
    for (int h = 0; h < kNumHarmonics; ++h)
    {
        const int v = waves ? waves->getHarmonic (table, h) : 0;
        levels[(size_t) h] = (float) v / 255.0f;
        const float cx = bars.getX() + step * ((float) h + 0.5f);
        auto slot = juce::Rectangle<float> (slotW, bars.getHeight()).withCentre ({ cx, bars.getCentreY() });

        // the slot, sunk in the panel
        g.setColour (juce::Colour (0xff060607));
        g.fillRoundedRectangle (slot.expanded (2.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff120806), slot.getX(), slot.getY(), juce::Colour (0xff0a0504), slot.getRight(), slot.getY(), false));
        g.fillRoundedRectangle (slot, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine (juce::roundToInt (slot.getBottom() + 2.5f), slot.getX(), slot.getRight());

        // the lit part, from the bottom, like a lamp behind a red filter
        if (v > 0)
        {
            auto lit = slot.reduced (2.0f).withTop (slot.getBottom() - 2.0f - (slot.getHeight() - 4.0f) * levels[(size_t) h]);
            g.setColour (colours::ledOn.withAlpha (0.18f));
            g.fillRoundedRectangle (lit.expanded (4.0f, 3.0f), 4.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff5a3a), lit.getX(), lit.getY(), juce::Colour (0xffc81c0e), lit.getRight(), lit.getY(), false));
            g.fillRoundedRectangle (lit, 1.5f);
            g.setColour (juce::Colour (0xffffc0a0).withAlpha (0.7f));
            g.fillRect (lit.getX() + lit.getWidth() * 0.32f, lit.getY() + 1.0f, 2.0f, lit.getHeight() - 2.0f);
        }

        g.setFont (Fonts::mono (11.0f));
        g.setColour (waves ? colours::vfd : colours::vfdDim.withAlpha (0.4f));
        g.drawText (waves ? juce::String (v) : juce::String ("-"), juce::Rectangle<float> (step, 14.0f).withCentre ({ cx, bars.getY() - 12.0f }), juce::Justification::centred);
        g.setFont (Fonts::bold (11.5f));
        g.setColour (colours::label);
        g.drawText (juce::String (h + 1), juce::Rectangle<float> (step, 14.0f).withCentre ({ cx, bars.getBottom() + 14.0f }), juce::Justification::centred);
    }

    // oscilloscope: one period of the sum of the harmonics (sines), as a preview
    drawVfdWindow (g, scope);
    auto screen = scope.reduced (10.0f);
    g.setColour (colours::vfd.withAlpha (0.12f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (juce::roundToInt (screen.getX() + screen.getWidth() * (float) i / 4.0f), screen.getY(), screen.getBottom());
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine (juce::roundToInt (screen.getY() + screen.getHeight() * (float) i / 4.0f), screen.getX(), screen.getRight());
    g.setColour (colours::vfd.withAlpha (0.25f));
    g.drawHorizontalLine (juce::roundToInt (screen.getCentreY()), screen.getX(), screen.getRight());

    g.setFont (Fonts::mono (15.0f));
    g.setColour (colours::vfdDim);
    g.drawText (juce::String (-8 - table), screen.withHeight (18.0f), juce::Justification::topRight);

    if (waves)
    {
        constexpr int points = 240;
        std::array<float, points + 1> ys {};
        float peak = 0.0f;
        for (int k = 0; k <= points; ++k)
        {
            float sum = 0.0f;
            for (int h = 0; h < kNumHarmonics; ++h)
                sum += levels[(size_t) h] * std::sin (juce::MathConstants<float>::twoPi * (float) ((h + 1) * k) / (float) points);
            ys[(size_t) k] = sum;
            peak = juce::jmax (peak, std::abs (sum));
        }
        juce::Path trace;
        for (int k = 0; k <= points; ++k)
        {
            const float x = screen.getX() + screen.getWidth() * (float) k / (float) points;
            const float y = screen.getCentreY() - (peak > 0.0f ? ys[(size_t) k] / peak : 0.0f) * screen.getHeight() * 0.42f;
            if (k == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
        }
        g.setColour (colours::vfd.withAlpha (0.25f));
        g.strokePath (trace, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::vfd);
        g.strokePath (trace, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setFont (Fonts::mono (13.0f));
        g.setColour (colours::vfdDim);
        g.drawFittedText (tr ("Dump the waveforms from the OSCar to edit them here."), screen.reduced (16.0f).toNearestInt(), juce::Justification::centred, 3);
    }
    g.setFont (Fonts::label (11.5f).withExtraKerningFactor (0.05f));
    g.setColour (colours::label);
    g.drawText (lang::upper (tr ("Preview (sum of sines)")), scope.withY (scope.getBottom() + 6.0f).withHeight (16.0f), juce::Justification::centred);
}

void WavesPanel::mouseDown (const juce::MouseEvent& e) { draw (e); }
void WavesPanel::mouseDrag (const juce::MouseEvent& e) { draw (e); }

void WavesPanel::draw (const juce::MouseEvent& e)
{
    if (! proc.getDocument().getWaves() || e.position.x < bars.getX() || e.position.x >= bars.getRight()
        || e.position.y < bars.getY() - 30.0f || e.position.y > bars.getBottom() + 10.0f)
        return;
    const int h = juce::jlimit (0, kNumHarmonics - 1, (int) ((e.position.x - bars.getX()) / (bars.getWidth() / (float) kNumHarmonics)));
    const int v = juce::jlimit (0, 255, juce::roundToInt ((bars.getBottom() - 2.0f - e.position.y) / (bars.getHeight() - 4.0f) * 255.0f));
    proc.getDocument().setHarmonic (table, h, v);
}

//==============================================================================
// Tauntek keys
//==============================================================================
namespace
{
    enum class KeyKind { Normal, Save, Replaces };
    struct KeyInfo { int key; const char* text; KeyKind kind; const char* plugin; };

    // From Tauntek's control-key chart (ctlkeys.jpg): hold TUNE/TRANSPOSE, press and release the key.
    // Key 0 = lowest C of the 37-key keyboard.
    const KeyInfo kKeys[] = {
        { 0,  "Load patches from save area 1", KeyKind::Replaces, nullptr },
        { 1,  "Save patches to save area 1",   KeyKind::Save,     nullptr },
        { 2,  "Load patches from save area 2", KeyKind::Replaces, nullptr },
        { 3,  "Save patches to save area 2",   KeyKind::Save,     nullptr },
        { 20, "Random arp mode",               KeyKind::Normal,   nullptr },
        { 21, "Disable patch CC dump",         KeyKind::Normal,   "CC DUMP light" },
        { 22, "Enable patch CC dump",          KeyKind::Normal,   "CC DUMP light" },
        { 23, "Load factory patches",          KeyKind::Replaces, nullptr },
        { 24, "Load B patches",                KeyKind::Replaces, nullptr },
        { 25, "Load C patches",                KeyKind::Replaces, nullptr },
        { 26, "Velocity to VCF env amount",    KeyKind::Normal,   nullptr },
        { 27, "Velocity not used",             KeyKind::Normal,   nullptr },
        { 29, "PWM slowest rate",              KeyKind::Normal,   "Voice > Tauntek > PWM Rate" },
        { 30, "PWM slower rate",               KeyKind::Normal,   "Voice > Tauntek > PWM Rate" },
        { 31, "PWM normal rate",               KeyKind::Normal,   "Voice > Tauntek > PWM Rate" },
        { 32, "PWM fastest rate",              KeyKind::Normal,   "Voice > Tauntek > PWM Rate" },
        { 33, "Disable LFO sync",              KeyKind::Normal,   "Voice > Tauntek > LFO Sync" },
        { 34, "Enable LFO sync",               KeyKind::Normal,   "Voice > Tauntek > LFO Sync" },
        { 35, "Enable LFO to oscillator 1",    KeyKind::Normal,   "Voice > Tauntek > LFO > Osc 1" },
        { 36, "Disable LFO to oscillator 1",   KeyKind::Normal,   "Voice > Tauntek > LFO > Osc 1" },
    };

    bool isBlack (int key) { const int n = key % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

    juce::Colour kindColour (KeyKind k)
    {
        return k == KeyKind::Replaces ? juce::Colour (0xffe0442e) : k == KeyKind::Save ? colours::warn : juce::Colour (0xff1a1a1a);
    }
}

KeysPanel::KeysPanel()
{
    tauntekButton.onClick = [] { juce::URL ("http://www.tauntek.com/OSCar.htm").launchInDefaultBrowser(); };
    tauntekButton.setTooltip (tr ("Tauntek's OSCar page: firmware, documentation and the control-key chart."));
    addAndMakeVisible (tauntekButton);
}

void KeysPanel::resized()
{
    auto area = juce::Rectangle<float> (0, 0, (float) width, (float) height);
    cards = area.removeFromTop (128.0f).reduced (8.0f, 8.0f);
    keyboard = area.reduced (8.0f, 6.0f);
    tauntekButton.setBounds (cards.getRight() - 210, cards.getY() + 12, 196, 34);
    addAndMakeVisible (wear);
    setupWear (*this, wear, { juce::Rectangle<float> (0, 0, (float) width, (float) height) }, { keyboard }, {});
}

void KeysPanel::paint (juce::Graphics& g)
{
    drawInset (g, { 0, 0, (float) width, (float) height });

    // explanation cards
    drawGroup (g, cards, tr ("tauntek shortcuts on the oscar"));
    auto c = cards.reduced (16.0f, 12.0f);
    g.setFont (Fonts::bold (16.0f));
    g.setColour (colours::text);
    g.drawText (tr ("Hold TUNE/TRANSPOSE, then press and release a key."), c.removeFromTop (22.0f), juce::Justification::centredLeft);
    c.removeFromTop (4.0f);
    drawText (g, c.removeFromTop (40.0f).withTrimmedRight (230.0f),
              tr ("Dumps: hold INSERT, press VOICE (or SEQ, a WAVEFORM button, or STORE for all), then SPACE. Program change on/off: hold SPACE and press key -9. These keys act on the OSCar only: the plugin can't trigger them over MIDI."), 13.0f, colours::label);

    auto legend = c.removeFromTop (20.0f);
    auto chip = [&g, &legend] (juce::Colour col, const juce::String& t)
    {
        g.setColour (col);
        g.fillRoundedRectangle (legend.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 14.0f), 3.0f);
        legend.removeFromLeft (6.0f);
        g.setFont (Fonts::label (13.0f));
        g.setColour (colours::label);
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 4.0f;
        g.drawText (t, legend.removeFromLeft (tw), juce::Justification::centredLeft);
        legend.removeFromLeft (22.0f);
    };
    chip (kindColour (KeyKind::Replaces), tr ("Replaces the voices in memory: save a bank in the plugin first"));
    chip (kindColour (KeyKind::Save), tr ("Overwrites a save area"));

    // keyboard
    auto kb = keyboard;
    const int whites = 22;
    const float ww = kb.getWidth() / (float) whites;
    std::map<int, juce::Rectangle<float>> rects;
    int wi = 0;
    for (int k = 0; k < 37; ++k)
        if (! isBlack (k))
            rects[k] = { kb.getX() + ww * (float) wi++, kb.getY(), ww, kb.getHeight() };
    for (int k = 0; k < 37; ++k)
        if (isBlack (k))
        {
            const auto& left = rects[k - 1];
            rects[k] = { left.getRight() - ww * 0.32f, kb.getY(), ww * 0.64f, kb.getHeight() * 0.62f };
        }

    auto infoFor = [] (int k) -> const KeyInfo*
    {
        for (const auto& i : kKeys)
            if (i.key == k) return &i;
        return nullptr;
    };

    auto drawLabel = [&g] (juce::Rectangle<float> r, const juce::String& text, juce::Colour ink, float size)
    {
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
        auto rotated = juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre());
        g.setFont (Fonts::bold (size));
        g.setColour (ink);
        g.drawFittedText (text, rotated.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
        g.restoreState();
    };

    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < 37; ++k)
        {
            if (isBlack (k) != (pass == 1))
                continue;
            auto r = rects[k].reduced (pass == 0 ? 1.0f : 0.0f, 0.0f);
            const auto* info = infoFor (k);
            if (pass == 0)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f1ea), r.getX(), r.getY(), juce::Colour (0xffd9d4ca),
                                                         r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, 3.0f);
                if (info != nullptr && info->kind != KeyKind::Normal)
                {
                    g.setColour (kindColour (info->kind).withAlpha (0.18f));
                    g.fillRoundedRectangle (r, 3.0f);
                }
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (r, 3.0f, 1.0f);
                if (info != nullptr)
                    drawLabel (r.withTop (r.getY() + r.getHeight() * 0.63f).withTrimmedBottom (info->plugin != nullptr ? 38.0f : 6.0f),
                               tr (info->text), info->kind == KeyKind::Normal ? juce::Colour (0xff1a1a1a) : kindColour (info->kind).darker (0.3f), 12.5f);
            }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.fillRoundedRectangle (r.translated (1.5f, 3.0f), 3.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2c30), r.getX(), r.getY(), juce::Colour (0xff0b0b0c),
                                                         r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, 3.0f);
                if (info != nullptr && info->kind != KeyKind::Normal)
                {
                    g.setColour (kindColour (info->kind).withAlpha (0.55f));
                    g.drawRoundedRectangle (r.reduced (1.5f), 3.0f, 2.0f);
                }
                if (info != nullptr)
                    drawLabel (r.reduced (1.0f, 6.0f), tr (info->text),
                               info->kind == KeyKind::Normal ? juce::Colours::white : kindColour (info->kind).brighter (0.4f), 11.5f);
            }
        }

    // plugin equivalents under the keyboard labels
    g.setFont (Fonts::label (11.0f));
    for (const auto& i : kKeys)
        if (i.plugin != nullptr && ! isBlack (i.key))
        {
            auto r = rects[i.key];
            g.setColour (juce::Colour (0xff6b4a2a));
            g.drawFittedText (tr ("Plugin: %1", { tr (i.plugin) }), r.removeFromBottom (34.0f).reduced (3.0f, 2.0f).toNearestInt(),
                              juce::Justification::centred, 3, 0.7f);
        }
}

//==============================================================================
// Setup
//==============================================================================
SetupPanel::SetupPanel (OscarProcessor& p, OscarEditor& e) : proc (p), editor (e)
{
    outBox.setTooltip (tr ("MIDI output connected to the OSCar's MIDI IN"));
    inBox.setTooltip (tr ("MIDI input connected to the OSCar's MIDI OUT (dumps, program changes, CC dumps)"));
    outBox.onChange = [this]
    {
        const int i = outBox.getSelectedId() - 2;
        proc.setOutputDevice (juce::isPositiveAndBelow (i, outIds.size()) ? outIds[i] : juce::String());
    };
    inBox.onChange = [this]
    {
        const int i = inBox.getSelectedId() - 2;
        proc.setInputDevice (juce::isPositiveAndBelow (i, inIds.size()) ? inIds[i] : juce::String());
    };
    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem (tr ("Channel %1", { juce::String (ch) }), ch);
    channelBox.setTooltip (tr ("MIDI channel of the OSCar (program changes, Live CC and MIDI Thru)"));
    channelBox.onChange = [this] { proc.setChannel (channelBox.getSelectedId()); };
    rescanButton.onClick = [this] { refreshPorts(); };

    followButton.onClick = [this] { proc.setFollowOscar (! proc.getFollowOscar()); };
    followButton.setTooltip (tr ("When you pick a voice on the OSCar, the plugin shows it."));
    selectAfterButton.onClick = [this] { proc.setSelectAfterSend (! proc.getSelectAfterSend()); };
    selectAfterButton.setTooltip (tr ("After sending the bank, select the edited voice on the OSCar so you hear it."));
    liveButton.onClick = [this] { proc.setLiveCc (! proc.getLiveCc()); };
    liveButton.setTooltip (tr ("Every edit is sent to the OSCar as CC at once, and clicking a voice plays it on the OSCar."));
    thruButton.onClick = [this] { proc.setMidiThru (! proc.getMidiThru()); };
    thruButton.setTooltip (tr ("Notes and pitch bend reaching this plugin from the DAW are played on the OSCar. Off if your DAW already sends MIDI to the OSCar (External Instrument...), to avoid double notes."));
    clockButton.onClick = [this] { proc.setClockOut (! proc.getClockOut()); };
    clockButton.setTooltip (tr ("While the DAW plays, the plugin sends MIDI clock to the OSCar (24 per quarter note), with Start, Continue and Stop. Voices set to Sync = MIDI clock then follow the DAW's tempo. Off if the DAW already sends clock to the OSCar's port."));

    auto& up = proc.getUpdater();
    checkButton.onClick = [&up] { up.checkNow(); };
    autoButton.onClick = [&up] { up.setAutoCheck (! up.getAutoCheck()); };
    notesButton.onClick = [this] { editor.showUpdateDialog(); };
    installButton.onClick = [this] { editor.showUpdateDialog(); };
    up.changes.addChangeListener (this);

    frenchButton.onClick = [this] { proc.setLanguage ("fr"); };
    englishButton.onClick = [this] { proc.setLanguage ("en"); };
    frenchButton.setTooltip (juce::String::fromUTF8 ("Interface en fran\xc3\xa7" "ais"));
    englishButton.setTooltip ("English interface");

    for (auto* c : std::initializer_list<juce::Component*> { &outBox, &inBox, &channelBox, &rescanButton, &followButton,
                                                             &selectAfterButton, &liveButton, &thruButton, &clockButton,
                                                             &checkButton, &autoButton, &notesButton, &installButton, &frenchButton, &englishButton })
        addAndMakeVisible (c);

    for (auto* t : { &help, &logView })
    {
        t->setMultiLine (true);
        t->setReadOnly (true);
        t->setCaretVisible (false);
        t->setScrollbarsShown (true);
        addAndMakeVisible (t);
    }
    help.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    help.setColour (juce::TextEditor::textColourId, colours::label);
    help.setFont (Fonts::label (14.0f));
    logView.setFont (Fonts::mono (13.0f));
    help.setText (tr ("CONNECTIONS\nOSCar MIDI OUT -> interface MIDI IN, interface MIDI OUT -> OSCar MIDI IN. Choose both ports on the left.\n\nON THE OSCAR\nProgram change: hold SPACE and press key -9 (LED -9 lit), so the plugin and the OSCar play the same voice. Patch CC dump: hold TUNE/TRANSPOSE and press the 'Enable patch CC dump' key (see the Tauntek keys page). The OSCar then reports each voice it loads and the plugin shows exactly what it plays. If the OSCar stops following after a power cycle, do these two steps again.\n\nRECEIVE: hold INSERT, press VOICE (or SEQ, a WAVEFORM button, or STORE for all three), then SPACE. Everything received is backed up (Banks page).\n\nSEND: the OSCar's MIDI only accepts complete banks, so \"Send voices to synth\" rewrites all 36 voices. What the OSCar held (when known) and what is sent are both backed up.\n\nLIVE CC: edits are sent at once as CC (half resolution: 0-127); Send stores the exact values. Every parameter can also be automated from the DAW.\n\nMIDI THRU: play the OSCar from another keyboard through this track. Notes and pitch bend only; nothing is passed during a transfer; the OSCar's own notes coming back through the DAW are not sent twice.\n\nMIDI CLOCK: set Sync to MIDI clock (Voice page) so the arpeggiator and the sequencer follow the clock the OSCar receives; Division sets the steps. With 'Send the DAW's MIDI clock' on, that clock comes from the DAW's tempo and transport."),
                  juce::dontSendNotification);

    refreshPorts();
}

SetupPanel::~SetupPanel()
{
    proc.getUpdater().changes.removeChangeListener (this);
}

void SetupPanel::refreshPorts()
{
    auto fill = [] (juce::ComboBox& box, juce::StringArray& ids, const juce::Array<juce::MidiDeviceInfo>& devices,
                    const juce::String& current, const juce::String& noneText)
    {
        box.clear (juce::dontSendNotification);
        ids.clear();
        box.addItem (noneText, 1);
        int selected = 1;
        for (const auto& d : devices)
        {
            ids.add (d.identifier);
            box.addItem (d.name, ids.size() + 1);
            if (d.identifier == current)
                selected = ids.size() + 1;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
    };
    fill (outBox, outIds, juce::MidiOutput::getAvailableDevices(), proc.getMidi().getOutputId(), tr ("(none)"));
    fill (inBox, inIds, juce::MidiInput::getAvailableDevices(), proc.getMidi().getInputId(), tr ("(none)"));
}

void SetupPanel::refresh()
{
    channelBox.setSelectedId (proc.getChannel(), juce::dontSendNotification);
    followButton.setLit (proc.getFollowOscar());
    selectAfterButton.setLit (proc.getSelectAfterSend());
    liveButton.setLit (proc.getLiveCc());
    thruButton.setLit (proc.getMidiThru());
    clockButton.setLit (proc.getClockOut());
    frenchButton.setLit (proc.getLanguage() == "fr");
    englishButton.setLit (proc.getLanguage() == "en");

    auto& up = proc.getUpdater();
    autoButton.setLit (up.getAutoCheck());
    const bool configured = up.getState() != Updater::State::Disabled;
    checkButton.setEnabled (configured && up.getState() != Updater::State::Checking && up.getState() != Updater::State::Installing);
    autoButton.setEnabled (configured);
    notesButton.setEnabled (up.getRelease().version.isNotEmpty());
    installButton.setVisible (up.getState() == Updater::State::Available);

    const auto text = proc.getLog().joinIntoString ("\n");
    if (text != logView.getText())
    {
        logView.setText (text, juce::dontSendNotification);
        logView.moveCaretToEnd();
    }
    repaint();
}

void SetupPanel::resized()
{
    const auto cols = splitColumns ({ 0, 0, (float) width, (float) height }, { 520, 776 });
    groups.clear();

    {
        auto c = cols[0].reduced (8.0f).toNearestInt();
        auto midi = c.removeFromTop (300);
        groups.push_back ({ midi.toFloat(), "midi" });
        auto g = midi.reduced (14, 12).withTrimmedBottom (22);
        portsLabels = g.removeFromLeft (70).toFloat();
        auto r = g.removeFromTop (30);
        outBox.setBounds (r);
        g.removeFromTop (8);
        r = g.removeFromTop (30);
        inBox.setBounds (r);
        g.removeFromTop (8);
        r = g.removeFromTop (30);
        channelBox.setBounds (r.removeFromLeft (130));
        r.removeFromLeft (8);
        rescanButton.setBounds (r.removeFromLeft (96).withHeight (34));
        englishButton.setBounds (r.removeFromRight (82).withHeight (34));
        r.removeFromRight (6);
        frenchButton.setBounds (r.removeFromRight (92).withHeight (34));
        g.removeFromTop (8);
        portsLabels = portsLabels.withHeight (116.0f);
        const int bh = 32;
        for (auto* b : { &followButton, &selectAfterButton, &liveButton, &clockButton })
        {
            b->setBounds (g.removeFromTop (bh).withX (g.getX() - 70).withWidth (g.getWidth() + 70));
            g.removeFromTop (4);
        }

        c.removeFromTop (8);
        auto thru = c.removeFromTop (130);
        groups.push_back ({ thru.toFloat(), tr ("midi thru and cc dump") });
        auto t = thru.reduced (14, 12).withTrimmedBottom (22);
        thruButton.setBounds (t.removeFromTop (32));
        t.removeFromTop (6);
        dumpText = t.toFloat();

        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("updates") });
        auto u = c.reduced (14, 12).withTrimmedBottom (22);
        u = u.withSizeKeepingCentre (u.getWidth(), 36 + 8 + 34);
        updateText = u.removeFromTop (36).toFloat();
        u.removeFromTop (8);
        auto row = u.removeFromTop (34);
        checkButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (8);
        autoButton.setBounds (row.removeFromLeft (190));
        row.removeFromLeft (8);
        notesButton.setBounds (row);
        installButton.setFace (updateText.toNearestInt().removeFromRight (170).withSizeKeepingCentre (170, 34));
    }

    {
        auto c = cols[1].reduced (8.0f).toNearestInt();
        auto h = c.removeFromTop (400);
        groups.push_back ({ h.toFloat(), tr ("how it works") });
        help.setBounds (h.reduced (12, 10).withTrimmedBottom (20));
        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("log") });
        logView.setBounds (c.reduced (12, 10).withTrimmedBottom (22));
    }
    addAndMakeVisible (wear);
    setupWear (*this, wear, {}, {}, {});
}

void SetupPanel::paint (juce::Graphics& g)
{
    for (const auto& [area, label] : groups)
        drawGroup (g, area, label);

    g.setFont (Fonts::bold (12.0f).withExtraKerningFactor (0.08f));
    g.setColour (colours::label);
    g.drawText (tr ("OUT"), portsLabels.withHeight (30.0f), juce::Justification::centredLeft);
    g.drawText (tr ("IN"), portsLabels.withHeight (30.0f).translated (0, 38.0f), juce::Justification::centredLeft);
    g.drawText (tr ("CHANNEL"), portsLabels.withHeight (30.0f).translated (0, 76.0f), juce::Justification::centredLeft);

    juce::String dump;
    switch (proc.getCcDumpState())
    {
        case OscarProcessor::CcDumpState::Active:  dump = tr ("Patch CC dump: on. The plugin shows what the OSCar plays when you change voice on it."); break;
        case OscarProcessor::CcDumpState::Missing: dump = tr ("Patch CC dump: not received. Turn it on (TUNE/TRANSPOSE + 'Enable patch CC dump') so the plugin follows the sounds you load on the OSCar."); break;
        case OscarProcessor::CcDumpState::Unknown: dump = tr ("Patch CC dump: change voice on the OSCar to check it is on."); break;
    }
    drawText (g, dumpText, dump, 13.0f, proc.getCcDumpState() == OscarProcessor::CcDumpState::Missing ? colours::warn : colours::dim);
    drawText (g, updateText.withTrimmedRight (installButton.isVisible() ? 180.0f : 0.0f),
              tr ("Version %1.", { Updater::currentVersion() }) + " " + proc.getUpdater().getStatusText(), 13.0f, colours::label);
}

} // namespace panels
