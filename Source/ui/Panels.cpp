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

    juce::String switchLabel (const ParamDef& p)
    {
        return juce::String (p.name) + (p.labelsVerified ? "" : " ?");
    }

    juce::String unverifiedText() { return tr ("Values checked against the Tauntek firmware; the names come from community documentation and are not confirmed yet."); }

    // MIDI clock (Tauntek firmware, routine 0x0A35)
    constexpr int kExtTrigger = 5;   // Trigger "Ext": arp and sequencer step on the MIDI clock
    // clock ticks per step {1, 2, 3, 4, 6, 8, 12, 16} at 24 per quarter note, chosen by Tempo >> 5
    const char* const kDivisions[8] = { "1/96", "1/48", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T" };

    std::vector<Selector::Item> itemsFor (const ParamDef& p)
    {
        std::vector<Selector::Item> items;
        for (const auto& c : p.choices)
            items.push_back ({ c.value, juce::String (c.label) });
        return items;
    }

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
// Voice
//==============================================================================
VoicePanel::VoicePanel (OscarProcessor& p) : proc (p)
{
    auto edit = [this] (const char* id) { return [this, id] (int v) { proc.editParam (proc.getSelectedSlot(), def (id), v); }; };

    auto addKnob = [&] (const char* id, KnobStyle style)
    {
        auto k = std::make_unique<Knob> (def (id).name, style);
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

    const std::pair<const char*, const char*> faderIds[] = { { "filtAttack", "Att" }, { "filtDecay", "Dec" }, { "filtSustain", "Sus" },
                                                             { "filtRelease", "Rel" }, { "volAttack", "Att" }, { "volDecay", "Dec" },
                                                             { "volSustain", "Sus" }, { "volRelease", "Rel" } };
    for (const auto& [id, label] : faderIds)
    {
        auto f = std::make_unique<Fader> (label);
        f->onChange = edit (id);
        f->setTooltip (juce::String (def (id).section) + " " + def (id).name + " (0-254)");
        addAndMakeVisible (*f);
        faders[id] = std::move (f);
    }

    for (auto* id : { "wave1", "wave2", "octaveShift", "osc2Semi", "pwmRate", "trigger", "glideType", "function",
                      "arpDir", "arpMode", "octaveSet", "tuneSemi", "lfoWave" })
    {
        const auto& d = def (id);
        auto s = std::make_unique<Selector> (switchLabel (d), std::string (id) == "lfoWave");
        s->setItems (itemsFor (d));
        s->onChange = edit (id);
        s->setTooltip (d.labelsVerified ? tr ("Values and names checked against the Tauntek firmware and chart.") : unverifiedText());
        addAndMakeVisible (*s);
        selectors[id] = std::move (s);
    }

    const auto& ft = def ("filterType");
    for (const auto& c : ft.choices)
    {
        auto* b = filterTypes.add (new LedButton (juce::String (c.label).replace ("no kb", "nk"), LedButton::LedPlace::Below));
        b->setTooltip (juce::String (c.label) + (ft.labelsVerified ? juce::String() : ". " + unverifiedText()));
        const int value = c.value;
        b->onClick = [this, value] { proc.editParam (proc.getSelectedSlot(), def ("filterType"), value); };
        addAndMakeVisible (b);
    }

    lfoToOsc1.setTooltip (tr ("Tauntek: the LFO also modulates oscillator 1 (same as TUNE/TRANSPOSE + the 'LFO to Oscillator 1' keys)."));
    lfoSync.setTooltip (tr ("Tauntek: LFO hard sync (same as TUNE/TRANSPOSE + the 'LFO Sync' keys)."));
    lfoToOsc1.onClick = [this]
    {
        const int v = getParam (proc.getDocument().getVoice (proc.getSelectedSlot()), def ("lfoToOsc1"));
        proc.editParam (proc.getSelectedSlot(), def ("lfoToOsc1"), v == 0 ? 1 : 0);
    };
    lfoSync.onClick = [this]
    {
        const int v = getParam (proc.getDocument().getVoice (proc.getSelectedSlot()), def ("lfoSync"));
        proc.editParam (proc.getSelectedSlot(), def ("lfoSync"), v == 0 ? 1 : 0);
    };
    addAndMakeVisible (lfoToOsc1);
    addAndMakeVisible (lfoSync);

    tempo.setTooltip (tr ("Global tempo (CC 54, sent with Live CC). Not stored in voices; can be automated from the DAW. When the voice follows the MIDI clock, it sets the division. In Internal, below 32 the arpeggiator steps on every MIDI clock tick."));
    tempoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getHostParameters(), "tempo", tempo.slider);
    tempo.setValueText ([this] (int t)
    {
        if (followsClock())
            return juce::String (kDivisions[juce::jlimit (0, 7, t >> 5)]);
        return t < 32 ? juce::String ("MIDI 1/96") : juce::String (t);
    });
    addAndMakeVisible (tempo);

    // Firmware (routine 0x0A35): with Trigger = Ext the arpeggiator and the sequencer step every
    // {1, 2, 3, 4, 6, 8, 12, 16} MIDI clock ticks (24 per quarter note), chosen by Tempo >> 5.
    syncSelector.setItems ({ { 0, tr ("Internal") }, { 1, tr ("MIDI clock") } });
    syncSelector.setTooltip (tr ("MIDI clock: the arpeggiator and the sequencer step on the MIDI clock the OSCar receives (Trigger = Ext, stored in the voice). The clock can come from the DAW (Setup page) or from any MIDI device. Internal: they follow the Tempo knob."));
    syncSelector.onChange = [this] (int v)
    {
        const int slot = proc.getSelectedSlot();
        const int trigger = getParam (proc.getDocument().getVoice (slot), def ("trigger"));
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
    };
    std::vector<Selector::Item> divisions;
    for (int z = 0; z < 8; ++z)
        divisions.push_back ({ z, kDivisions[z] });
    divisionSelector.setItems (divisions);
    divisionSelector.setTooltip (tr ("Steps of the arpeggiator and the sequencer when the voice follows the MIDI clock (T = triplet). Sets the global Tempo (CC 54, sent with Live CC)."));
    divisionSelector.onChange = [this] (int z)
    {
        if (tempoWatch != nullptr)
            tempoWatch->setValueAsCompleteGesture ((float) (z * 32 + 16));
    };
    addAndMakeVisible (syncSelector);
    addAndMakeVisible (divisionSelector);
    if (auto* t = proc.getHostParameters().getParameter ("tempo"))
    {
        tempoWatch = std::make_unique<juce::ParameterAttachment> (*t, [this] (float) { refreshClock(); });
        tempoWatch->sendInitialUpdate();
    }

    layout();
}

VoicePanel::~VoicePanel()
{
    tempoWatch.reset();
    tempoAttachment.reset();
}

bool VoicePanel::followsClock() const
{
    return getParam (proc.getDocument().getVoice (proc.getSelectedSlot()), def ("trigger")) == kExtTrigger;
}

void VoicePanel::refreshClock()
{
    const bool ext = followsClock();
    const int t = juce::roundToInt (proc.getHostParameters().getRawParameterValue ("tempo")->load());
    syncSelector.setValue (ext ? 1 : 0);
    divisionSelector.setValue (juce::jlimit (0, 7, t >> 5));
    divisionSelector.setAlpha (ext ? 1.0f : 0.45f);
    tempo.repaint();
}

Knob& VoicePanel::knob (const char* id)            { return *knobs.at (id); }
Selector& VoicePanel::selector (const char* id, bool) { return *selectors.at (id); }

void VoicePanel::refresh()
{
    const auto& v = proc.getDocument().getVoice (proc.getSelectedSlot());
    for (auto& [id, k] : knobs)      k->setValue (getParam (v, def (id.c_str())));
    for (auto& [id, f] : faders)     f->setValue (getParam (v, def (id.c_str())));
    for (auto& [id, s] : selectors)  s->setValue (getParam (v, def (id.c_str())));

    const int type = getParam (v, def ("filterType"));
    const auto& ft = def ("filterType");
    for (int i = 0; i < filterTypes.size(); ++i)
        filterTypes[i]->setLit (ft.choices[(size_t) i].value == type);
    lfoToOsc1.setLit (getParam (v, def ("lfoToOsc1")) == 0);   // 0 = On
    lfoSync.setLit (getParam (v, def ("lfoSync")) == 1);
    refreshClock();
}

void VoicePanel::layout()
{
    columns = splitColumns ({ 0, 0, (float) width, (float) height }, { 306, 314, 362, 300 });
    groups.clear();
    auto col = [this] (int i) { return columns[(size_t) i].reduced (8.0f).toNearestInt(); };

    // 1: oscillators + wheels
    {
        auto c = col (0);
        auto osc = c.removeFromTop (243);
        groups.push_back ({ osc.toFloat(), tr ("oscillators") });
        auto g = osc.reduced (8, 10);
        const int sw = (g.getWidth() - 10) / 2;
        selector ("wave1").setBounds (g.getX(), g.getY(), sw, 46);
        selector ("wave2").setBounds (g.getRight() - sw, g.getY(), sw, 46);
        selector ("octaveShift").setBounds (g.getX(), g.getY() + 54, sw, 46);
        selector ("osc2Semi").setBounds (g.getRight() - sw, g.getY() + 54, sw, 46);
        spread ({ &knob ("detune"), &knob ("pulseWidth"), &knob ("oscBalance"), &knob ("noiseBalance") },
                { g.getX(), g.getY() + 110, g.getWidth(), 99 }, 64, 99);

        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("wheels") });
        auto w = c.reduced (12, 10).withTrimmedBottom (22);
        spread ({ &knob ("bendAmount"), &knob ("mwPitch"), &knob ("mwFilter") }, w.withSizeKeepingCentre (w.getWidth(), 97), 70, 97);
    }

    // 2: filter + Tauntek
    {
        auto c = col (1);
        auto f = c.removeFromTop (277);
        groups.push_back ({ f.toFloat(), tr ("filter") });
        auto g = f.reduced (8, 10);
        const int bw = 40, gap = 5, total = filterTypes.size() * bw + (filterTypes.size() - 1) * gap;
        int x = g.getCentreX() - total / 2;
        for (auto* b : filterTypes)
        {
            b->setBounds (x, g.getY(), bw, 34);
            x += bw + gap;
        }
        auto area = juce::Rectangle<int> (g.getX(), g.getY() + 42, g.getWidth(), 200);
        const int cx = area.getCentreX();
        knob ("filterFreq").setBounds (cx - 65, area.getY() + 10, 130, 166);
        knob ("resonance").setBounds (cx - 65 - 8 - 70, area.getY(), 70, 97);
        knob ("filterSep").setBounds (cx - 65 - 8 - 70, area.getY() + 103, 70, 97);
        knob ("envFilter").setBounds (cx + 65 + 8, area.getY(), 70, 97);
        knob ("filterDrive").setBounds (cx + 65 + 8, area.getY() + 103, 70, 97);

        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), "tauntek" });
        auto t = c.reduced (10, 10).withTrimmedBottom (22);
        auto row = t.withSizeKeepingCentre (t.getWidth(), 46);
        lfoToOsc1.setBounds (row.getX(), row.getY() + 4, 70, 36);
        lfoSync.setBounds (row.getX() + 78, row.getY() + 4, 70, 36);
        selector ("pwmRate").setBounds (row.getRight() - 120, row.getY(), 120, 46);
    }

    // 3: envelopes + modulation
    {
        auto c = col (2);
        auto envs = c.removeFromTop (220);
        auto fe = envs.removeFromLeft ((envs.getWidth() - 8) / 2);
        envs.removeFromLeft (8);
        groups.push_back ({ fe.toFloat(), tr ("filter env") });
        groups.push_back ({ envs.toFloat(), tr ("amp env") });
        auto place = [this] (juce::Rectangle<int> grp, std::initializer_list<const char*> ids)
        {
            auto g = grp.reduced (6, 8);
            const int fw = 38, total = 4 * fw + 3 * 2;
            int x = g.getCentreX() - total / 2;
            for (auto* id : ids)
            {
                faders.at (id)->setBounds (x, g.getY(), fw, 184);
                x += fw + 2;
            }
        };
        place (fe, { "filtAttack", "filtDecay", "filtSustain", "filtRelease" });
        place (envs, { "volAttack", "volDecay", "volSustain", "volRelease" });

        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("modulation") });
        auto m = c.reduced (10, 12).withTrimmedBottom (22);
        selector ("lfoWave").setBounds (m.getCentreX() - 125, m.getY(), 250, 30);
        auto knobsRow = m.withTrimmedTop (40);
        spread ({ &knob ("lfoRate"), &knob ("introDelay"), &knob ("lfoPitch"), &knob ("lfoFilter") },
                knobsRow.withSizeKeepingCentre (knobsRow.getWidth(), 97), 70, 97);
    }

    // 4: performance + MIDI clock
    {
        auto c = col (3);
        auto clock = c.removeFromBottom (88);
        c.removeFromBottom (8);
        groups.push_back ({ clock.toFloat(), tr ("midi clock") });
        auto k = clock.reduced (8, 10).withTrimmedBottom (22);
        const int kw = (k.getWidth() - 8) / 2;
        syncSelector.setBounds (k.getX(), k.getY(), kw, 46);
        divisionSelector.setBounds (k.getRight() - kw, k.getY(), kw, 46);

        groups.push_back ({ c.toFloat(), tr ("performance") });
        auto g = c.reduced (8, 12).withTrimmedBottom (22);
        const int sw = (g.getWidth() - 8) / 2;
        const char* ids[] = { "trigger", "glideType", "function", "octaveSet", "arpDir", "arpMode", "tuneSemi" };
        for (int i = 0; i < 7; ++i)
            selector (ids[i]).setBounds (g.getX() + (i % 2) * (sw + 8), g.getY() + (i / 2) * 56, sw, 46);
        auto row = juce::Rectangle<int> (g.getX(), g.getY() + 4 * 56 + 16, g.getWidth(), 99);
        spread ({ &knob ("glideRate"), &knob ("gateTime"), &tempo }, row, 70, 99);
    }
}

void VoicePanel::resized() {}

void VoicePanel::paint (juce::Graphics& g)
{
    for (const auto& c : columns)
        drawInset (g, c);
    for (const auto& gr : groups)
        drawGroup (g, gr.area, gr.label);

    // big-knob tick legend
    auto& f = knob ("filterFreq");
    g.setFont (Fonts::label (10.5f));
    g.setColour (colours::faint);
    g.drawText (tr ("closed"), f.getX() - 10, f.getY() + 112, 50, 12, juce::Justification::centredLeft);
    g.drawText (tr ("open"), f.getRight() - 40, f.getY() + 112, 50, 12, juce::Justification::centredRight);
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
    amount.onChange = [this] (int v) { proc.genSettings().amount = v / 100.0; refresh(); };
    amount.setTooltip (tr ("Vary: how far from the voice (above 75 % leaves the factory ranges). Breed: how much mutation."));
    addAndMakeVisible (amount);

    generateButton.fontHeight = 20.0f;
    generateButton.onClick = [this] { proc.generate(); };
    generateButton.setTooltip (tr ("Generate into the selected voice. With Live CC the OSCar plays it at once; its memory is only written by \"Send voices to synth\"."));
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
    for (int i = 0; i < kNumVoices; ++i)
        voices.push_back ({ i, voiceItemText (proc.getDocument(), i).trim() });
    parentA.setItems (voices);
    parentB.setItems (voices);
    parentA.setValue (s.parentA);
    parentB.setValue (s.parentB);
    for (auto* c : { &parentA, &parentB })
        c->setAlpha (s.mode == OscarProcessor::GenMode::Breed ? 1.0f : 0.45f);

    amount.setValue (juce::roundToInt (s.amount * 100.0));
    amount.setLabel (s.mode == OscarProcessor::GenMode::Vary ? tr ("Variation")
                     : s.mode == OscarProcessor::GenMode::Breed ? tr ("Mutation") : tr ("Not used by New"));
    amount.setAlpha (s.mode == OscarProcessor::GenMode::New ? 0.45f : 1.0f);

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
        auto c = columns[0].reduced (8.0f).toNearestInt();
        auto m = c.removeFromTop (76);
        groups.push_back ({ m.toFloat(), tr ("mode") });
        auto row = m.reduced (10, 10).removeFromTop (34);
        const int bw = (row.getWidth() - 16) / 3;
        for (int i = 0; i < modes.size(); ++i)
            modes[i]->setBounds (row.getX() + i * (bw + 8), row.getY(), bw, 34);

        c.removeFromTop (8);
        auto f = c.removeFromTop (148);
        groups.push_back ({ f.toFloat(), tr ("family (new)") });
        auto padRow = f.reduced (10, 12).removeFromTop (94);
        std::initializer_list<juce::Component*> list { pads[0], pads[1], pads[2], pads[3], pads[4] };
        spread (list, padRow, 70, 94);

        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("parents (breed)") });
        auto p = c.reduced (12, 14);
        parentA.setBounds (p.getX(), p.getY(), (p.getWidth() - 12) / 2, 46);
        parentB.setBounds (p.getRight() - (p.getWidth() - 12) / 2, p.getY(), (p.getWidth() - 12) / 2, 46);
        parentsNote = p.withTrimmedTop (58).withTrimmedBottom (26).toFloat();
    }

    // B: amount, generate, history navigation
    {
        auto c = columns[1].reduced (8.0f).toNearestInt();
        groups.push_back ({ c.toFloat(), tr ("generate") });
        auto g = c.reduced (12, 12).withTrimmedBottom (22);
        amount.setBounds (g.getCentreX() - 85, g.getY() + 2, 170, 166);
        amountNote = juce::Rectangle<int> (g.getX(), g.getY() + 174, g.getWidth(), 36).toFloat();
        generateButton.setBounds (g.getCentreX() - 130, g.getY() + 216, 260, 64);
        auto row = juce::Rectangle<int> (g.getX(), g.getY() + 296, g.getWidth(), 34);
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
        keepButton.setBounds (buttons);
        h.removeFromBottom (8);
        historyArea = h.toFloat();
    }
}

void GeneratorPanel::paint (juce::Graphics& g)
{
    for (const auto& c : columns)
        drawInset (g, c);
    for (const auto& [area, label] : groups)
        drawGroup (g, area, label);

    // amount scale words
    g.setFont (Fonts::bold (12.0f).withExtraKerningFactor (0.1f));
    g.setColour (colours::label);
    g.drawText (tr ("SUBTLE"), amount.getBounds().translated (-82, 108).withSize (80, 14), juce::Justification::centredRight);
    g.setColour (colours::vfd);
    g.drawText (tr ("WILD"), amount.getBounds().translated (172, 108).withSize (80, 14), juce::Justification::centredLeft);

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

    for (auto* b : std::initializer_list<juce::Component*> { &renameButton, &copyButton, &pasteButton, &swapButton, &revertButton,
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
    if (bankList.getSelectedRow() != slot)
    {
        bankList.selectRow (slot);
        bankList.scrollToEnsureRowIsOnscreen (slot);
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
    bankList.scrollToEnsureRowIsOnscreen (proc.getSelectedSlot());
}

int BanksPanel::getNumRows()
{
    return kNumVoices;
}

void BanksPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& doc = proc.getDocument();
    if (selected)
    {
        g.setColour (colours::copper.withAlpha (0.85f));
        g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 2.0f);
    }
    else if (row == 12)
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
    if (updating || row < 0 || row == proc.getSelectedSlot())
        return;
    proc.chooseVoice (row);
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
    editor.askText (tr ("Save the bank"), tr ("Name of the bank (saved in Documents > OSCar Editor > Banks, with the voice names)."),
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

    {
        auto c = cols[0].reduced (8.0f).toNearestInt();
        groups.push_back ({ c.toFloat(), tr ("bank in the editor") });
        auto g = c.reduced (12, 12).withTrimmedBottom (22);
        auto buttons = g.removeFromBottom (78);
        g.removeFromBottom (8);
        bankList.setBounds (g.reduced (4));
        const int bw = (buttons.getWidth() - 24) / 4;
        juce::Component* row1[] = { &renameButton, &copyButton, &pasteButton, &swapButton };
        juce::Component* row2[] = { &revertButton, &selectButton, &exportVoiceButton, &importVoiceButton };
        for (int i = 0; i < 4; ++i)
        {
            row1[i]->setBounds (buttons.getX() + i * (bw + 8), buttons.getY(), bw, 34);
            row2[i]->setBounds (buttons.getX() + i * (bw + 8), buttons.getY() + 42, bw, 34);
        }
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

        auto buttons = g.removeFromBottom (78);
        g.removeFromBottom (8);
        g.removeFromTop (8);
        libraryList.setBounds (g.reduced (4));
        loadButton.setBounds (buttons.getX(), buttons.getY(), 220, 76);
        auto rest = buttons.withTrimmedLeft (232);
        const int bw = (rest.getWidth() - 16) / 3;
        juce::Component* row1[] = { &saveAsButton, &openFileButton, &exportSyxButton };
        juce::Component* row2[] = { &factoryButton, &folderButton, &sequencesButton };
        for (int i = 0; i < 3; ++i)
        {
            row1[i]->setBounds (rest.getX() + i * (bw + 8), rest.getY(), bw, 34);
            row2[i]->setBounds (rest.getX() + i * (bw + 8), rest.getY() + 42, bw, 34);
        }
        groups.push_back ({ libraryList.getBounds().toFloat().expanded (4.0f), {} });
    }
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
    sendButton.setBounds (top.removeFromRight (230).withSizeKeepingCentre (230, 40));
    g.removeFromTop (8);
    infoArea = g.removeFromTop (22).toFloat();
    g.removeFromTop (8);
    bars = g.toFloat();
}

void WavesPanel::paint (juce::Graphics& g)
{
    drawInset (g, { 0, 0, (float) width, (float) height });
    drawGroup (g, group, tr ("harmonics"));
    drawText (g, infoArea, info, 13.0f, colours::dim, juce::Justification::centredLeft);
    drawVfdWindow (g, bars);

    const auto& waves = proc.getDocument().getWaves();
    auto area = bars.reduced (12.0f, 10.0f);
    if (! waves)
    {
        g.setFont (Fonts::mono (15.0f));
        g.setColour (colours::vfdDim);
        g.drawText (tr ("Dump the waveforms from the OSCar to edit them here."), bars, juce::Justification::centred);
        return;
    }
    const float w = area.getWidth() / (float) kNumHarmonics;
    for (int h = 0; h < kNumHarmonics; ++h)
    {
        const int v = waves->getHarmonic (table, h);
        auto col = juce::Rectangle<float> (area.getX() + (float) h * w, area.getY(), w, area.getHeight() - 18.0f).reduced (3.0f, 0.0f);
        g.setColour (colours::vfd.withAlpha (0.08f));
        g.fillRect (col);
        const float bh = col.getHeight() * (float) v / 255.0f;
        // segmented, like a LED meter
        for (float y = col.getBottom() - 4.0f; y > col.getBottom() - bh; y -= 6.0f)
        {
            g.setColour (colours::vfd);
            g.fillRect (col.getX(), y, col.getWidth(), 4.0f);
        }
        g.setFont (Fonts::mono (11.0f));
        g.setColour (colours::vfdDim);
        g.drawText (juce::String (h + 1), juce::Rectangle<float> (col.getX(), col.getBottom() + 3.0f, col.getWidth(), 14.0f),
                    juce::Justification::centred);
        g.setColour (colours::vfd);
        g.drawText (juce::String (v), col.withHeight (14.0f), juce::Justification::centred);
    }
}

void WavesPanel::mouseDown (const juce::MouseEvent& e) { draw (e); }
void WavesPanel::mouseDrag (const juce::MouseEvent& e) { draw (e); }

void WavesPanel::draw (const juce::MouseEvent& e)
{
    auto area = bars.reduced (12.0f, 10.0f);
    if (! proc.getDocument().getWaves() || e.position.x < area.getX() || e.position.x >= area.getRight())
        return;
    const int h = juce::jlimit (0, kNumHarmonics - 1, (int) ((e.position.x - area.getX()) / (area.getWidth() / (float) kNumHarmonics)));
    const float usable = area.getHeight() - 18.0f;
    const int v = juce::jlimit (0, 255, juce::roundToInt ((1.0f - (e.position.y - area.getY()) / usable) * 255.0f));
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
        auto upd = c;
        groups.push_back ({ upd.toFloat(), tr ("midi thru, cc dump and updates") });
        auto u = upd.reduced (14, 12).withTrimmedBottom (22);
        thruButton.setBounds (u.removeFromTop (32));
        u.removeFromTop (6);
        dumpText = u.removeFromTop (30).toFloat();
        u.removeFromTop (4);
        updateText = u.removeFromTop (30).toFloat();
        auto row = u.removeFromTop (34);
        checkButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (8);
        autoButton.setBounds (row.removeFromLeft (190));
        row.removeFromLeft (8);
        notesButton.setBounds (row);
        installButton.setBounds (updateText.toNearestInt().removeFromRight (170).withSizeKeepingCentre (170, 34));
    }

    {
        auto c = cols[1].reduced (8.0f).toNearestInt();
        auto h = c.removeFromTop (290);
        groups.push_back ({ h.toFloat(), tr ("how it works") });
        help.setBounds (h.reduced (12, 10).withTrimmedBottom (20));
        c.removeFromTop (8);
        groups.push_back ({ c.toFloat(), tr ("log") });
        logView.setBounds (c.reduced (12, 10).withTrimmedBottom (22));
    }
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
