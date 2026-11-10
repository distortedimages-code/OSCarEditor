// The six pages of the editor, drawn in the Atelier style. Each one reads the
// processor/document in refresh() and sends edits back through the processor.

#pragma once

#include <JuceHeader.h>

#include "../PluginProcessor.h"
#include "Atelier.h"
#include "Lang.h"

class OscarEditor;

namespace panels
{
constexpr int width = 1304, height = 610;   // every page is laid out on this logical size
// printed ink rubbed off here and there, fine scratches (0: none); the oxide finish is more worn
inline int wearLevel() { return atelier::currentFinish() == 2 ? 2 : 1; }

// Puts 'wear' on top of 'page': its children and the given displays are kept clear; 'plates' are the
// darker column bands (their colour is what rubbed-off ink shows), 'handled' the places touched most.
void setupWear (juce::Component& page, atelier::WearLayer& wear, std::vector<juce::Rectangle<float>> plates,
                std::vector<juce::Rectangle<float>> displays, std::vector<atelier::WearLayer::Spot> handled,
                std::vector<juce::Path> extraKeepClear = {});

//==============================================================================
class VoicePanel : public juce::Component
{
public:
    explicit VoicePanel (OscarProcessor&);
    ~VoicePanel() override;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    // The RANDOM layer: while it is on, the knobs and faders, Transpose, Osc 2 Interval and Osc 2
    // Octave set their random amounts instead of their values.
    void setRandomEdit (bool on);
    bool isRandomEdit() const                       { return randomEdit; }

private:
    struct Group { juce::Rectangle<float> area; juce::String label; };
    atelier::Knob& knob (const char* id);
    atelier::Selector& selector (const char* id);
    atelier::RotarySwitch& rotary (const char* id);
    atelier::LedSelector& leds (const char* id);
    void layout();

    // Rows of controls: the rows share the height evenly, the controls of a row its width (a 'join'ed
    // control sticks to the next one), and every control of a row sits on the same centre line.
    struct Cell { juce::Component* c; int w; bool join = false; };
    std::pair<int, int> extent (juce::Component&) const;   // { centre line from the top, height }
    void place (juce::Component&, int x, int centreY, int w);
    std::vector<juce::Range<int>> arrange (juce::Rectangle<int> area, const std::vector<std::vector<Cell>>& rows);   // each row's span

    // A switch whose high bit is a separate button (Function + Hold, Glide + Fix, filter Type + Kbd track):
    // the selector picks the low part, the button the flag (values 4-6 = with the flag).
    void editFlagged (const char* id, int base, bool flag);
    int paramValue (const char* id) const;

    OscarProcessor& proc;
    std::map<std::string, std::unique_ptr<atelier::Knob>> knobs;
    std::map<std::string, std::unique_ptr<atelier::Fader>> faders;
    std::map<std::string, std::unique_ptr<atelier::Selector>> selectors;        // waveforms
    std::map<std::string, std::unique_ptr<atelier::RotarySwitch>> rotaries;     // four positions or more
    std::map<std::string, std::unique_ptr<atelier::LedSelector>> ledSelectors;  // two or three positions
    atelier::LedButton noTrack { "No track", atelier::LedButton::LedPlace::Below },
                       hold { "Hold", atelier::LedButton::LedPlace::Below },
                       glideFix { "Fix", atelier::LedButton::LedPlace::Below };
    atelier::LedButton lfoToOsc1 { "LFO > Osc 1", atelier::LedButton::LedPlace::Right },
                       lfoSync { "LFO Sync", atelier::LedButton::LedPlace::Right };
    juce::Component lfoStack;   // where the two LFO buttons go, one above the other
    atelier::LedRow octaveLeds { "Octave" };
    atelier::VfdNumber tempo { "Tempo", 1, 254 };   // dragged up or down, like an old synth display
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAttachment;

    // MIDI clock: Sync sets Trigger to Ext (arp and sequencer step on the MIDI clock),
    // Division picks the steps through the global Tempo.
    void refreshClock();
    bool followsClock() const;
    void setSync (int v);
    atelier::LedSelector syncLeds { TRANS ("Sync") };
    atelier::RotarySwitch divisionSwitch { "Ext Trigger Divide" };
    std::unique_ptr<juce::ParameterAttachment> tempoWatch;
    int lastTrigger = 1;   // Multi

    std::vector<juce::Rectangle<float>> columns;       // the four blocks
    std::vector<Group> groups;
    struct Text { juce::Rectangle<float> area; juce::String text; };
    std::vector<Text> titles, separators;              // section titles; the lines between the two rows of a block
    std::vector<juce::Line<float>> dividers;           // lines between the sub-sections of a block
    // printed lines: where the sound and the modulations go (the audio path in the accent colour)
    struct Route { std::vector<juce::Point<float>> points; bool audio = false; bool dot = false; juce::String label; };
    std::vector<Route> routes;
    std::vector<juce::Point<float>> junctions;         // where a line branches
    juce::Rectangle<float> delayBracket, delayWord;    // the filter envelope's DELAY, from Sustain to Release
    atelier::CachedPainting plates;                    // what never moves, drawn once
    void paintPlates (juce::Graphics&);


    atelier::WearLayer wear;                           // on top of everything, controls cut out
    void setupWear (int level);

    std::map<std::string, std::unique_ptr<atelier::RandomTouch>> touches;   // above the wear
    bool randomEdit = false;
    void placeTouches();
};

//==============================================================================
class GeneratorPanel : public juce::Component
{
public:
    explicit GeneratorPanel (OscarProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    OscarProcessor& proc;
    juce::OwnedArray<atelier::LedButton> modes, locks;
    juce::OwnedArray<atelier::Pad> pads;
    atelier::Selector parentA { TRANS ("Parent A") }, parentB { TRANS ("Parent B") };
    atelier::Knob amount { TRANS ("Amount"), atelier::KnobStyle::Brass, 100 };
    atelier::CopperButton generateButton { TRANS ("Generate") };
    atelier::LedButton prevButton { TRANS ("Previous") }, nextButton { TRANS ("Next") }, auditionButton { TRANS ("Audition (Live CC)") },
                       originalButton { TRANS ("Back to original") };
    atelier::CopperButton keepButton { TRANS ("Keep") };
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> groups;
    std::vector<juce::Rectangle<float>> columns;
    juce::Rectangle<float> historyArea, amountNote, parentsNote, locksNote;
    atelier::WearLayer wear;
};

//==============================================================================
// A/B morph: two voices and a crossfader; the selected voice follows the blend.
class MorphPanel : public juce::Component, private juce::Timer
{
public:
    explicit MorphPanel (OscarProcessor&);
    ~MorphPanel() override;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;   // follows the gliding blend
    void refreshLoad();              // Load lit while a chosen voice waits to be loaded
    OscarProcessor& proc;
    // Choosing a voice in the list (or Selected voice) only prepares it; Load puts it at that end.
    // Until then the blend keeps the voices A and B already loaded.
    atelier::Selector voiceA { TRANS ("Voice A") }, voiceB { TRANS ("Voice B") };
    int pendingA = -1, pendingB = -1;   // chosen, not loaded yet (-1: nothing waiting)
    atelier::LedButton takeA { TRANS ("Selected voice"), atelier::LedButton::LedPlace::None },
                       takeB { TRANS ("Selected voice"), atelier::LedButton::LedPlace::None };
    atelier::LedButton loadA { TRANS ("Load") }, loadB { TRANS ("Load") };
    juce::Slider fader;   // the crossfader: host parameter "morph"
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAttachment;
    atelier::Knob smoothing { TRANS ("Smoothing"), atelier::KnobStyle::Chrome, 200 };
    atelier::CopperButton keepButton { TRANS ("Keep the blend") };
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> groups;
    std::vector<juce::Rectangle<float>> columns;
    juce::Rectangle<float> preview, readout, info, letterA, letterB, bigA, bigB, summaryA, summaryB;   // summaries: the switches, which flip half way
    float shownPosition = -1.0f;
    atelier::WearLayer wear;
};

//==============================================================================
class BanksPanel : public juce::Component, private juce::ListBoxModel
{
public:
    BanksPanel (OscarProcessor&, OscarEditor&);
    ~BanksPanel() override;
    void refresh();
    void rescanLibrary();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Entry { juce::File file; juce::String kind; juce::Time when; };

    // the bank list (left) uses this model; the library list has its own
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;

    struct LibraryModel : juce::ListBoxModel
    {
        explicit LibraryModel (BanksPanel& p) : owner (p) {}
        int getNumRows() override;
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
        BanksPanel& owner;
    };

    void loadSelectedEntry();
    void saveBankAs();

    OscarProcessor& proc;
    OscarEditor& editor;
    LibraryModel libraryModel { *this };
    juce::ListBox bankList { "Bank", this }, libraryList { "Library", &libraryModel };
    std::vector<Entry> entries, shown;
    int filter = 0;   // 0 all, 1 banks, 2 voices, 3 backups
    bool updating = false;

    atelier::LedButton renameButton { TRANS ("Rename") }, copyButton { TRANS ("Copy") }, pasteButton { TRANS ("Paste") }, swapButton { TRANS ("Swap...") },
                       revertButton { TRANS ("Revert") }, selectButton { TRANS ("Select on synth") }, exportVoiceButton { TRANS ("Save voice...") },
                       importVoiceButton { TRANS ("Import voice...") }, initButton { TRANS ("Init") };
    atelier::LedButton allButton { TRANS ("All") }, banksButton { TRANS ("My banks") }, voicesButton { TRANS ("My voices") },
                       backupsButton { TRANS ("Backups") };
    atelier::CopperButton loadButton { TRANS ("Load into editor") };
    atelier::LedButton saveAsButton { TRANS ("Save bank as...") }, openFileButton { TRANS ("Open file...") }, exportSyxButton { TRANS ("Export .syx...") },
                       factoryButton { TRANS ("Factory voices") }, folderButton { TRANS ("Open folder") }, sequencesButton { TRANS ("Send sequences...") };
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> groups;
    juce::Rectangle<float> libraryNote;
    atelier::WearLayer wear;
};

//==============================================================================
class WavesPanel : public juce::Component
{
public:
    WavesPanel (OscarProcessor&, OscarEditor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void draw (const juce::MouseEvent&);
    OscarProcessor& proc;
    OscarEditor& editor;
    juce::OwnedArray<atelier::LedButton> tables;
    atelier::CopperButton sendButton { TRANS ("Send waveforms") };
    juce::Rectangle<float> group, bars, scope, infoArea;   // bars: the slots only (values above, numbers below)
    juce::String info;
    int table = 0;
    atelier::WearLayer wear;
};

//==============================================================================
class KeysPanel : public juce::Component
{
public:
    KeysPanel();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    atelier::LedButton tauntekButton { TRANS ("Tauntek OSCar page") };
    juce::Rectangle<float> keyboard, cards;
    atelier::WearLayer wear;
};

//==============================================================================
class SetupPanel : public juce::Component, private juce::ChangeListener
{
public:
    SetupPanel (OscarProcessor&, OscarEditor&);
    ~SetupPanel() override;
    void refresh();
    void refreshPorts();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    OscarProcessor& proc;
    OscarEditor& editor;
    juce::ComboBox outBox, inBox, channelBox;
    juce::StringArray outIds, inIds;
    atelier::LedButton rescanButton { TRANS ("Rescan") }, followButton { TRANS ("Follow the OSCar's voice changes") },
                       selectAfterButton { TRANS ("Select the voice after sending") }, liveButton { TRANS ("Live CC (Tauntek)") },
                       thruButton { TRANS ("MIDI Thru: play the OSCar from this track") },
                       clockButton { TRANS ("Send the DAW's MIDI clock to the OSCar") };
    atelier::LedButton checkButton { TRANS ("Check now") }, autoButton { TRANS ("Check automatically") }, notesButton { TRANS ("What's new") };
    atelier::CopperButton installButton { TRANS ("Install update") };
    atelier::LedButton frenchButton { juce::String::fromUTF8 ("Fran\xc3\xa7" "ais") }, englishButton { "English" };
    juce::TextEditor help, logView;
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> groups;
    juce::Rectangle<float> portsLabels, dumpText, updateText;
    atelier::WearLayer wear;
};

} // namespace panels
