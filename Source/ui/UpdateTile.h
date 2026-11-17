// The tile that offers a new version when the window opens: the version, what it brings in a few
// words (the headlines of the release notes), a link to the release page, and a button that
// installs it in place, the app, the AU and the VST3 at once (macOS; elsewhere it opens the
// download page). It follows the updater: downloading, installed (relaunch the app, or restart
// the DAW), failed (try again, or the page).

#pragma once

#include <JuceHeader.h>

#include "../Updater.h"
#include "Atelier.h"

class UpdateTile : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    // 'language' gives "en" or "fr"; 'standalone' is true in the app (it can relaunch itself).
    UpdateTile (Updater&, std::function<juce::String()> language, bool standalone);
    ~UpdateTile() override;

    std::function<void()> onDismiss;   // Later, Skip, OK, the cross: hide the tile
    std::function<void()> onQuit;      // the app after Relaunch: quit, so that the new one opens

    void refresh();                    // texts and buttons, from the updater's state
    bool hasSomethingToShow() const;   // a version on offer, being installed, installed or failed
    int getPreferredHeight() const     { return preferredHeight; }   // shadow included

    static constexpr int tileWidth = 452;   // the plate; the component has room for its shadow around it
    static constexpr int shadow = 18;
    // Updates installed in place (macOS); elsewhere the download page. Screenshots made elsewhere set it.
   #if JUCE_MAC
    static inline bool inPlaceUpdates = true;
   #else
    static inline bool inPlaceUpdates = false;
   #endif

    bool hitTest (int x, int y) override     { return getLocalBounds().reduced (shadow).contains (x, y); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override  { refresh(); }
    void timerCallback() override;

    struct Link : juce::Component
    {
        juce::String text;
        bool dim = false;              // a quieter link, on the right (Skip this version)
        std::function<void()> onClick;
        Link() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }
    };
    struct Cross : juce::Button
    {
        Cross() : juce::Button ("close") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    Updater& updater;
    std::function<juce::String()> language;
    bool standalone;

    juce::String kicker, version, youHave, body, footnote;
    std::vector<Updater::Headline> items;
    bool busy = false;                 // downloading: the moving bar
    float phase = 0.0f;

    atelier::CopperButton mainButton { {} };
    atelier::LedButton secondButton { {}, atelier::LedButton::LedPlace::None };
    Link link, skipLink;
    Cross cross;
    std::function<void()> mainAction, secondAction;

    int preferredHeight = 260;
    juce::Rectangle<int> bodyArea, itemsArea, footArea, barArea;
    int layoutHeight();                // lays the parts out; returns the height they need

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateTile)
};
