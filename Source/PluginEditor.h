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

    static constexpr int baseWidth = 1400, baseHeight = 800;   // everything is laid out on this size, then scaled

    // Shared by the pages
    void runFileChooser (const juce::String& title, const juce::String& patterns, bool save,
                         const juce::String& defaultName, std::function<void (const juce::File&)> onChosen);
    void showResult (const juce::Result& r, const juce::String& successText = {});
    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  const juce::String& okText, std::function<void (const juce::String&)> onOk);
    void confirmThenSendVoices();
    void confirmThenSendWaves();
    void confirmThenSendSequences();
    void renameSelected();
    void swapSelected (juce::Component& anchor);
    void exportSelectedVoice();
    void importIntoSelectedVoice();
    void loadBankFromFile();
    void exportBankSyx();
    void showUpdateDialog();
    void showPage (int index);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
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

    juce::OwnedArray<atelier::LedButton> tabs;
    std::unique_ptr<panels::VoicePanel> voicePage;
    std::unique_ptr<panels::GeneratorPanel> generatorPage;
    std::unique_ptr<panels::BanksPanel> banksPage;
    std::unique_ptr<panels::WavesPanel> wavesPage;
    std::unique_ptr<panels::KeysPanel> keysPage;
    std::unique_ptr<panels::SetupPanel> setupPage;
    std::vector<juce::Component*> pages;
    int page = 0;

    juce::OwnedArray<atelier::LedButton> memory;

    atelier::LedButton undoButton { {} }, redoButton { {} }, selectAfterButton { {} }, liveButton { "Live CC" }, thruButton { "MIDI Thru" };
    atelier::CopperButton sendButton { {} };
    juce::String message;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AlertWindow> textDialog;

    int lastProgress = -1;
    double lastProgressTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscarEditor)
};
