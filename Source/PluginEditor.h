#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/Atelier.h"
#include "ui/Panels.h"

class OscarEditor : public juce::AudioProcessorEditor,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit OscarEditor (OscarProcessor&);
    ~OscarEditor() override;

    void resized() override;

    static constexpr int baseWidth = 1400, baseHeight = 900;   // everything is laid out on this size, then scaled

    // Shared by the pages
    void runFileChooser (const juce::String& title, const juce::String& patterns, const juce::File& folder,
                         std::function<void (const juce::File&)> onChosen);
    // Asks for a name, then saves straight into 'folder' (no file browser); never overwrites.
    void saveInto (const juce::File& folder, const juce::String& extension, const juce::String& title,
                   const juce::String& message, const juce::String& initialName,
                   std::function<juce::Result (const juce::File&)> save);
    // The part after the dialog: returns the file written, or File() if nothing was.
    juce::File saveNamed (const juce::File& folder, const juce::String& extension, const juce::String& name,
                          const std::function<juce::Result (const juce::File&)>& save);
    juce::Result exportVoiceTo (const juce::File&);     // the selected voice
    juce::Result exportSyxTo (const juce::File&);
    void showResult (const juce::Result& r, const juce::String& successText = {});
    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  const juce::String& okText, std::function<void (const juce::String&)> onOk);
    void confirmThenSendVoices();
    void confirmThenSendWaves();
    void confirmThenSendSequences();
    void renameSelected();
    void swapSelected (juce::Component& anchor);
    void initSelected();
    void checkOutdatedCopies();   // copies still named OSCar Editor next to OXIde: offers to bin them
    void exportSelectedVoice();
    void importIntoSelectedVoice();
    void loadBankFromFile();
    void exportBankSyx();
    void showUpdateDialog();
    void showPage (int index);
    void toggleRandomEdit();   // the RANDOM button: its strip and its layer on the Voice page, on or off
    void toggleScaleStrip();   // the SCALE button: its strip
    void toggleSaveStrip();    // the SAVE button: its strip (on the computer / in the OSCar)
    void openSave (bool inOscar)  { saveInOscar = inOscar; showStrip (Strip::Save); }
    static constexpr int banksPageIndex = 3, setupPageIndex = 6;   // Voice, Generator, Morph, Banks, Waveforms, Tauntek keys, Setup

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void libraryChanged();
    void timerCallback() override;
    void refreshAll();
    void refreshStatus();
    void layout();
    void paintBackdrop (juce::Graphics&);
    void showVoiceMenu();
    void applyTexts();
    void createPages();

    struct Backdrop : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    // Voice number and name in an orange window, with previous / next arrows.
    struct Display : juce::Component, juce::SettableTooltipClient
    {
        std::function<void()> onClick;
        juce::String number, name, state;
        bool warn = false;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    };

    OscarProcessor& proc;
    atelier::Look look;
    juce::TooltipWindow tooltips { this, 600 };

    Backdrop root;
    atelier::WoodCheek leftCheek { true, 11 }, rightCheek { false, 23 };
    Display display;
    atelier::LedButton prevVoice { "<", atelier::LedButton::LedPlace::None }, nextVoice { ">", atelier::LedButton::LedPlace::None };
    atelier::LedButton updateButton { {} };
    atelier::LedButton channelButton { {} };   // the OSCar on another channel: the plugin takes it

    juce::OwnedArray<atelier::LedButton> tabs;
    std::unique_ptr<panels::VoicePanel> voicePage;
    std::unique_ptr<panels::GeneratorPanel> generatorPage;
    std::unique_ptr<panels::MorphPanel> morphPage;
    std::unique_ptr<panels::BanksPanel> banksPage;
    std::unique_ptr<panels::WavesPanel> wavesPage;
    std::unique_ptr<panels::KeysPanel> keysPage;
    std::unique_ptr<panels::SetupPanel> setupPage;
    std::vector<juce::Component*> pages;
    int page = 0;

    juce::OwnedArray<atelier::LedButton> memory;

    // RANDOM and SCALE: two buttons in the footer; each opens its strip where the memories are
    enum class Strip { None, Random, Scale, Save };
    Strip strip = Strip::None;
    void showStrip (Strip);
    atelier::LedButton randomButton { "Supermod" }, scaleButton { "Scale" };
    // the RANDOM strip
    atelier::Selector shapeSelector { "Shape" }, loopSelector { "Loop" }, polaritySelector { "Mode" }, bpmSelector { "BPM" };
    juce::Slider rateKnob;                  // synced: a division (clockwise faster); free: 10 s .. 10 ms
    atelier::VfdText rateText { "Rate" };
    atelier::LedButton syncButton { "Sync" }, newLoopButton { {} }, clearButton { {} };
    atelier::LedButton activeButton { {} };   // lit: the randoms play; off: all switched off, kept
    // the SCALE strip
    atelier::Selector scaleSelector { "Scale" }, rootSelector { "Root" };
    atelier::ScaleKeys scaleKeys;
    // the SAVE strip: two tabs. On the computer: the files of the Banks page. In the OSCar: the
    // selected voice into one memory, by hand (VOICE, STORE, its key: no MIDI message stores),
    // or the whole bank by SysEx (as Send to synth).
    atelier::LedButton saveButton { {} };
    atelier::LedButton computerTab { {} }, oscarTab { {} };
    atelier::LedButton saveVoiceButton { {} }, saveBankButton { {} }, saveSyxButton { {} }, folderButton { {} };
    atelier::Selector memorySelector { "Memory" };
    atelier::LedButton storeButton { {} }, doneButton { {} }, bankButton { {} };
    atelier::VfdText storeKeys { "Keys on the OSCar" };
    bool saveInOscar = true;     // the tab
    int saveTarget = -1, saveTargetFor = -1;   // the memory chosen, and for which selected voice
    void refreshSave();
    struct Blinker : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } blinker;
    int shownSteps = -1;
    double flashUntil = 0;
    void refreshRandom();       // the settings, as the processor has them
    void blinkRandom();         // the RANDOM light: a flash at each step; steady while its layer is on
    void askRecall();           // a project opened (Ask): recall its sound, or keep the OSCar's?
    bool recallAsked = false;

    atelier::LedButton undoButton { {} }, redoButton { {} }, liveButton { "Live CC" }, thruButton { "MIDI Thru" };
    atelier::CopperButton sendButton { {} };
    atelier::CachedPainting panelCache;   // the brushed panel, vents and logo
    juce::String message, shownLights;   // what the backdrop shows: repainted only when they change

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AlertWindow> textDialog;

    int lastProgress = -1;
    double lastProgressTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscarEditor)
};
