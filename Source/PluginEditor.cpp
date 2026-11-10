#include "PluginEditor.h"

using namespace oscar;
using namespace atelier;
using lang::tr;

namespace
{
    juce::String slotsText (juce::Array<int> slots)
    {
        std::sort (slots.begin(), slots.end(), [] (int a, int b) { return positionOfSlot (a) < positionOfSlot (b); });
        juce::StringArray s;
        for (auto i : slots)
            s.add (slotLabel (i));
        return s.joinIntoString (", ");
    }

    juce::File defaultFolder()
    {
        return OscarProcessor::getUserFolder();
    }

    // Layout of the 1400 x 900 window
    constexpr int innerX = 48, innerW = 1304;
    constexpr int headerY = 10, headerH = 60;
    constexpr int displayY = 78, displayH = 44;
    constexpr int tabsY = 130, tabsH = 34;
    constexpr int pageY = 172;
    constexpr int memoryY = 790, memoryH = 46;
    constexpr int footerY = 846, footerH = 44;

    const juce::Rectangle<float> leftVents  { 54.0f, 20.0f, 300.0f, 40.0f };
    const juce::Rectangle<float> rightVents { 1046.0f, 20.0f, 300.0f, 40.0f };
}

//==============================================================================
void OscarEditor::Display::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawVfdWindow (g, r);
    auto inner = r.reduced (16.0f, 0.0f);

    g.setFont (Fonts::mono (28.0f));
    const float numW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), number);
    g.setFont (Fonts::mono (21.0f));
    const float nameW = juce::jmin (230.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name));
    g.setFont (Fonts::mono (13.0f));
    const float stateW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), state);
    const float total = numW + 14.0f + nameW + 14.0f + stateW;
    float x = inner.getCentreX() - total * 0.5f;

    auto glowText = [&g] (const juce::String& t, juce::Rectangle<float> area, float size, juce::Colour c)
    {
        g.setFont (Fonts::mono (size));
        g.setColour (c.withAlpha (0.25f));
        g.drawText (t, area.translated (0.0f, 0.5f).expanded (1.0f, 0.0f), juce::Justification::centredLeft, true);
        g.setColour (c);
        g.drawText (t, area, juce::Justification::centredLeft, true);
    };
    glowText (number, { x, r.getY(), numW + 4.0f, r.getHeight() }, 28.0f, colours::vfd);
    x += numW + 14.0f;
    glowText (name, { x, r.getY(), nameW, r.getHeight() }, 21.0f, colours::vfd);
    x += nameW + 14.0f;
    glowText (state, { x, r.getY() + 2.0f, stateW + 4.0f, r.getHeight() }, 13.0f, warn ? colours::warn : colours::vfdDim);
}

//==============================================================================
OscarEditor::OscarEditor (OscarProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    root.setLookAndFeel (&look);
    panelCache.opaque = true;
    root.painter = [this] (juce::Graphics& g) { paintBackdrop (g); };
    root.setOpaque (true);
    addAndMakeVisible (root);

    root.addAndMakeVisible (leftCheek);
    root.addAndMakeVisible (rightCheek);

    display.onClick = [this] { showVoiceMenu(); };
    display.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    root.addAndMakeVisible (display);
    // in the keyboard's order: ... -2, -1, 1, 2 ...
    prevVoice.onClick = [this] { proc.chooseVoice (slotAtPosition ((positionOfSlot (proc.getSelectedSlot()) + kNumVoices - 1) % kNumVoices)); };
    nextVoice.onClick = [this] { proc.chooseVoice (slotAtPosition ((positionOfSlot (proc.getSelectedSlot()) + 1) % kNumVoices)); };
    root.addAndMakeVisible (prevVoice);
    root.addAndMakeVisible (nextVoice);

    updateButton.setLit (true);
    updateButton.onClick = [this] { showUpdateDialog(); };
    root.addChildComponent (updateButton);

    for (int i = 0; i < 7; ++i)
    {
        auto* t = tabs.add (new LedButton ({}));
        t->onClick = [this, i] { showPage (i); };
        root.addAndMakeVisible (t);
    }

    for (int i = 0; i < kNumVoices; ++i)
    {
        auto* m = memory.add (new LedButton (slotLabel (i), LedButton::LedPlace::Below));
        m->onClick = [this, i] { proc.chooseVoice (i); };
        root.addAndMakeVisible (m);
    }

    undoButton.onClick = [this] { proc.getDocument().undo(); };
    redoButton.onClick = [this] { proc.getDocument().redo(); };
    selectAfterButton.onClick = [this] { proc.setSelectAfterSend (! proc.getSelectAfterSend()); };
    liveButton.onClick = [this] { proc.setLiveCc (! proc.getLiveCc()); };
    thruButton.onClick = [this] { proc.setMidiThru (! proc.getMidiThru()); };
    sendButton.onClick = [this] { confirmThenSendVoices(); };
    for (auto* c : std::initializer_list<juce::Component*> { &undoButton, &redoButton, &selectAfterButton, &liveButton,
                                                             &thruButton, &sendButton })
        root.addAndMakeVisible (c);

    page = proc.getMidi().isOutputOpen() ? juce::jlimit (0, 6, proc.editorPage) : setupPageIndex;
    applyTexts();
    createPages();

    proc.getDocument().addChangeListener (this);
    proc.statusBroadcaster.addChangeListener (this);
    proc.getUpdater().changes.addChangeListener (this);
    proc.languageChanged.addChangeListener (this);

    layout();
    refreshAll();
    juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<OscarEditor> (this)]
                                 { if (safe != nullptr) safe->checkOutdatedCopies(); });

    setResizable (true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (980, 630, 2240, 1440);
    setSize (juce::roundToInt (baseWidth * proc.editorScale), juce::roundToInt (baseHeight * proc.editorScale));
    startTimerHz (10);

    proc.getUpdater().checkIfDue();
}

OscarEditor::~OscarEditor()
{
    stopTimer();
    proc.getDocument().removeChangeListener (this);
    proc.statusBroadcaster.removeChangeListener (this);
    proc.getUpdater().changes.removeChangeListener (this);
    proc.languageChanged.removeChangeListener (this);
    if (textDialog != nullptr)
        textDialog->exitModalState (0);
    textDialog.reset();
    root.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void OscarEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    proc.editorScale = scale;
    root.setBounds (0, 0, baseWidth, baseHeight);
    root.setTransform (juce::AffineTransform::scale (scale));
}

void OscarEditor::layout()
{
    leftCheek.setBounds (0, 0, 36, baseHeight);
    rightCheek.setBounds (baseWidth - 36, 0, 36, baseHeight);

    display.setBounds (510, displayY, 380, displayH);
    prevVoice.setBounds (466, displayY, 44, displayH + 3);
    nextVoice.setBounds (890, displayY, 44, displayH + 3);
    updateButton.setBounds (950, displayY + 4, 180, 36);

    const int tw = (innerW - 12 - 6 * 8) / 7;
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setBounds (innerX + 6 + i * (tw + 8), tabsY, tw, tabsH + 3);
    int x = innerX + 74;
    for (int pos = 0; pos < kNumVoices; ++pos)   // as on the keyboard: -12 .. -1, gap, 1 .. 24
    {
        if (pos == 12)
            x += 14;
        memory[slotAtPosition (pos)]->setBounds (x, memoryY + 4, 29, 38);
        x += 32;
    }

    auto f = juce::Rectangle<int> (innerX + 6, footerY, innerW - 12, footerH);
    undoButton.setBounds (f.removeFromLeft (92).withHeight (37));
    f.removeFromLeft (8);
    redoButton.setBounds (f.removeFromLeft (92).withHeight (37));
    sendButton.setFace (f.removeFromRight (240));
    f.removeFromRight (10);
    thruButton.setBounds (f.removeFromRight (124).withHeight (37));
    f.removeFromRight (8);
    liveButton.setBounds (f.removeFromRight (112).withHeight (37));
    f.removeFromRight (8);
    selectAfterButton.setBounds (f.removeFromRight (186).withHeight (37));
}

void OscarEditor::paintBackdrop (juce::Graphics& g)
{
    // the panel, its brushed lines, the vents and the logo never change: drawn once, then copied
    panelCache.draw (g, { 0, 0, baseWidth, baseHeight }, [this] (juce::Graphics& p)
    {
        p.setGradientFill (juce::ColourGradient (colours::panelTop, 0, 0, colours::panel, 0, 120, false));
        p.fillRect (0, 0, baseWidth, baseHeight);
        p.setGradientFill (juce::ColourGradient (colours::panel, 0, 400, colours::panelLow, 0, (float) baseHeight, false));
        p.fillRect (0, 400, baseWidth, baseHeight - 400);
        p.setColour (juce::Colours::white.withAlpha (0.012f));   // fine brushed lines
        for (int x = 36; x < baseWidth - 36; x += 3)
            p.drawVerticalLine (x, 0.0f, (float) baseHeight);
        drawVents (p, leftVents);
        drawVents (p, rightVents);
        const float logoH = 34.0f;
        drawLogo (p, { (float) baseWidth * 0.5f - logoWidth (logoH) * 0.5f, (float) headerY + 6.0f }, logoH, juce::Colour (0xffeeedea));
    });
    g.setFont (Fonts::bold (12.0f).withExtraKerningFactor (0.22f));
    g.setColour (juce::Colour (0xffa9acb1));
    g.drawText (tr ("EDITOR & LIBRARIAN FOR THE OSCAR"), juce::Rectangle<int> (400, headerY + 45, 600, 16), juce::Justification::centred);

    // status lights
    auto& midi = proc.getMidi();
    struct Light { const char* label; bool on; bool warnLabel; };
    const auto dumpState = proc.getCcDumpState();
    const Light left[] = { { "MIDI OUT", midi.isOutputOpen(), ! midi.isOutputOpen() }, { "MIDI IN", midi.isInputOpen(), false } };
    const auto dumpLabel = dumpState == OscarProcessor::CcDumpState::Missing ? tr ("CC DUMP OFF") : juce::String ("CC DUMP");
    const Light right[] = { { "LIVE CC", proc.getLiveCc(), false },
                            { dumpLabel.toRawUTF8(),
                              dumpState == OscarProcessor::CcDumpState::Active, dumpState == OscarProcessor::CcDumpState::Missing } };
    auto drawLights = [&g] (const Light* lights, int n, float x)
    {
        for (int i = 0; i < n; ++i)
        {
            drawLed (g, { x + 4.0f, (float) displayY + displayH * 0.5f }, 8.0f, lights[i].on);
            g.setFont (Fonts::bold (12.0f).withExtraKerningFactor (0.14f));
            g.setColour (lights[i].warnLabel ? colours::warn : juce::Colour (0xffc9cbcf));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), lights[i].label) + 6.0f;
            g.drawText (lights[i].label, juce::Rectangle<float> (x + 16.0f, (float) displayY, w, (float) displayH), juce::Justification::centredLeft);
            x += 16.0f + w + 20.0f;
        }
    };
    drawLights (left, 2, 56.0f);
    drawLights (right, 2, updateButton.isVisible() ? 1150.0f : 1120.0f);

    // memory strip
    g.setFont (Fonts::bold (13.0f));
    g.setColour (juce::Colour (0xffdcdee1));
    g.drawText (tr ("memory"), juce::Rectangle<int> (innerX + 2, memoryY, 74, 30), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    const int divider = memory[slotAtPosition (11)]->getRight() + 7;   // after -1
    g.drawVerticalLine (divider, (float) memoryY + 6.0f, (float) memoryY + 34.0f);

    // message
    const auto msgArea = juce::Rectangle<int> (redoButton.getRight() + 14, footerY, selectAfterButton.getX() - redoButton.getRight() - 24, 37);
    g.setFont (Fonts::mono (14.0f));
    g.setColour (colours::vfdDim);
    g.drawFittedText (message, msgArea, juce::Justification::centredLeft, 2, 0.85f);
}

//==============================================================================
void OscarEditor::showPage (int index)
{
    page = juce::jlimit (0, (int) pages.size() - 1, index);
    proc.editorPage = page;
    for (int i = 0; i < (int) pages.size(); ++i)
    {
        pages[(size_t) i]->setVisible (i == page);
        tabs[i]->setLit (i == page);
    }
    if (page == banksPageIndex)
        banksPage->rescanLibrary();   // also scrolls the bank list to the selected voice
}

void OscarEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &proc.languageChanged)
    {
        // rebuild every text: the pages are recreated, the window's own buttons relabelled
        applyTexts();
        createPages();
        root.repaint();
    }
    refreshAll();
}

void OscarEditor::applyTexts()
{
    display.setTooltip (tr ("Click to choose a voice. The arrows step through the bank."));
    const char* tabNames[] = { "Voice", "Generator", "Morph", "Banks", "Waveforms", "Tauntek keys", "Setup" };
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setButtonText (tr (tabNames[i]));
    for (int i = 0; i < memory.size(); ++i)
        memory[i]->setTooltip (tr ("Voice %1. An orange number means edits not yet sent to the OSCar.", { juce::String (slotLabel (i)) }));

    undoButton.setButtonText (tr ("Undo"));
    redoButton.setButtonText (tr ("Redo"));
    selectAfterButton.setButtonText (tr ("Then select voice"));
    sendButton.setButtonText (tr ("Send voices to synth"));
    selectAfterButton.setTooltip (tr ("After sending, select the edited voice on the OSCar so you hear the change."));
    liveButton.setTooltip (tr ("Tauntek: every edit is sent to the OSCar as CC straight away, and choosing a voice plays it on the OSCar. Off: edit the bank without touching the OSCar's sound."));
    thruButton.setTooltip (tr ("Play the OSCar from this DAW track (notes and pitch bend)."));
    sendButton.setTooltip (tr ("Writes the 36 voices into the OSCar's memory (the OSCar only accepts whole banks)."));
}

void OscarEditor::createPages()
{
    for (auto* pg : pages)
        root.removeChildComponent (pg);
    pages.clear();
    voicePage.reset(); generatorPage.reset(); morphPage.reset(); banksPage.reset(); wavesPage.reset(); keysPage.reset(); setupPage.reset();

    voicePage     = std::make_unique<panels::VoicePanel> (proc);
    generatorPage = std::make_unique<panels::GeneratorPanel> (proc);
    morphPage     = std::make_unique<panels::MorphPanel> (proc);
    banksPage     = std::make_unique<panels::BanksPanel> (proc, *this);
    wavesPage     = std::make_unique<panels::WavesPanel> (proc, *this);
    keysPage      = std::make_unique<panels::KeysPanel>();
    setupPage     = std::make_unique<panels::SetupPanel> (proc, *this);
    pages = { voicePage.get(), generatorPage.get(), morphPage.get(), banksPage.get(), wavesPage.get(), keysPage.get(), setupPage.get() };

    for (auto* pg : pages)
    {
        root.addChildComponent (pg);
        pg->setBounds (innerX, pageY, panels::width, panels::height);
    }
    showPage (page);
}

void OscarEditor::refreshAll()
{
    const auto& doc = proc.getDocument();
    const int slot = proc.getSelectedSlot();

    voicePage->refresh();
    generatorPage->refresh();
    morphPage->refresh();
    banksPage->refresh();
    wavesPage->refresh();
    setupPage->refresh();

    for (int i = 0; i < memory.size(); ++i)
    {
        memory[i]->setLit (i == slot);
        memory[i]->setTextColour (doc.hasUnsentEdits (i) ? colours::warn : (i == slot ? juce::Colours::white : colours::label));
    }

    undoButton.setEnabled (doc.canUndo());
    redoButton.setEnabled (doc.canRedo());
    selectAfterButton.setLit (proc.getSelectAfterSend());
    liveButton.setLit (proc.getLiveCc());
    thruButton.setLit (proc.getMidiThru());

    auto& up = proc.getUpdater();
    const bool showUpdate = up.getState() == Updater::State::Available || up.getState() == Updater::State::Installed;
    updateButton.setVisible (showUpdate);
    updateButton.setButtonText (up.getState() == Updater::State::Installed ? tr ("Restart the DAW")
                                                                          : tr ("Update %1", { up.getRelease().version }));

    refreshStatus();

    // The backdrop shows the status lights and the last message: repaint only those strips, and only
    // when they change. (Repainting the whole window on every edit made turning a knob stutter.)
    const auto newMessage = proc.getLastMessage().fromFirstOccurrenceOf ("  ", false, false);
    const auto& midi = proc.getMidi();
    const auto lights = juce::String (midi.isOutputOpen() ? "O" : "o") + (midi.isInputOpen() ? "I" : "i") + (proc.getLiveCc() ? "L" : "l")
                        + juce::String ((int) proc.getCcDumpState()) + (updateButton.isVisible() ? "U" : "u");
    if (lights != shownLights)
    {
        shownLights = lights;
        root.repaint (0, displayY - 4, baseWidth, displayH + 8);
    }
    if (newMessage != message)
    {
        message = newMessage;
        root.repaint (redoButton.getRight(), footerY, selectAfterButton.getX() - redoButton.getRight(), footerH);
    }
}

void OscarEditor::refreshStatus()
{
    auto& midi = proc.getMidi();
    const auto& doc = proc.getDocument();
    const int slot = proc.getSelectedSlot();

    const auto now = juce::Time::getMillisecondCounterHiRes();
    const int progress = proc.getReceiveProgress();
    if (progress != lastProgress)
    {
        lastProgress = progress;
        lastProgressTime = now;
    }

    juce::String state;
    bool warn = false;
    if (! midi.isOutputOpen())                                  { state = tr ("NO MIDI OUT"); warn = true; }
    else if (midi.isBusy())                                     state = tr ("SENDING %1 S", { juce::String (midi.busyRemainingMs() / 1000.0, 1) });
    else if (progress >= 0 && now - lastProgressTime < 4000.0)  state = tr ("RECEIVING %1", { juce::String (progress) });
    else if (doc.hasUnsentEdits (slot))                         { state = tr ("EDITED"); warn = true; }
    else if (doc.isOscarVoiceStateKnown())                      state = tr ("IN SYNC");
    else                                                        state = tr ("NOT DUMPED");

    const auto number = juce::String (slotLabel (slot));
    const auto name = lang::upper (doc.getDisplayName (slot));
    if (number != display.number || name != display.name || state != display.state || warn != display.warn)
    {
        display.number = number;
        display.name = name;
        display.state = state;
        display.warn = warn;
        display.repaint();
    }

    sendButton.setEnabled (midi.isOutputOpen() && ! midi.isBusy());
}

void OscarEditor::timerCallback()
{
    refreshStatus();
}

void OscarEditor::showVoiceMenu()
{
    static constexpr int initItem = 1000;   // static: usable in the callback without a capture (MSVC)
    juce::PopupMenu m;
    m.addItem (initItem, tr ("Init voice %1", { juce::String (slotLabel (proc.getSelectedSlot())) }));
    m.addSeparator();
    m.addSectionHeader (tr ("Voices"));
    for (int pos = 0; pos < kNumVoices; ++pos)
    {
        const int i = slotAtPosition (pos);
        m.addItem (i + 1, juce::String (slotLabel (i)).paddedLeft (' ', 3) + "   " + proc.getDocument().getDisplayName (i)
                              + (proc.getDocument().hasUnsentEdits (i) ? "   " + tr ("(edited)") : juce::String()),
                   true, i == proc.getSelectedSlot());
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (display).withMaximumNumColumns (2),
                     [safe = juce::Component::SafePointer<OscarEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         if (r == initItem)
                             safe->initSelected();
                         else
                             safe->proc.chooseVoice (r - 1);
                     });
}

void OscarEditor::initSelected()
{
    const int slot = proc.getSelectedSlot();
    proc.getDocument().initSlot (slot);
    proc.addLog (tr ("Voice %1 is now INIT, a plain starting sound. Undo brings the previous one back.", { juce::String (slotLabel (slot)) }));
}

//==============================================================================
void OscarEditor::askText (const juce::String& titleText, const juce::String& messageText, const juce::String& initial,
                           const juce::String& okText, std::function<void (const juce::String&)> onOk)
{
    if (textDialog != nullptr)
        textDialog->exitModalState (0);

    textDialog = std::make_unique<juce::AlertWindow> (titleText, messageText, juce::MessageBoxIconType::NoIcon, this);
    textDialog->setLookAndFeel (&look);
    textDialog->addTextEditor ("text", initial);
    textDialog->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    textDialog->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    textDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<OscarEditor> (this), cb = std::move (onOk)] (int r)
        {
            if (safe == nullptr || safe->textDialog == nullptr)
                return;
            const auto text = safe->textDialog->getTextEditorContents ("text");
            safe->textDialog->setVisible (false);
            if (r == 1)
                cb (text);
        }), false);
}

void OscarEditor::renameSelected()
{
    const int slot = proc.getSelectedSlot();
    askText (tr ("Rename voice %1", { juce::String (slotLabel (slot)) }),
             tr ("Names live in the plugin and in .oscbank files (the OSCar has no names)."),
             proc.getDocument().getName (slot), tr ("Rename"),
             [this, slot] (const juce::String& t) { proc.getDocument().rename (slot, t); });
}

void OscarEditor::swapSelected (juce::Component& anchor)
{
    const int slot = proc.getSelectedSlot();
    juce::PopupMenu m;
    m.addSectionHeader (tr ("Swap voice %1 with...", { juce::String (slotLabel (slot)) }));
    for (int pos = 0; pos < kNumVoices; ++pos)
        if (const int i = slotAtPosition (pos); i != slot)
            m.addItem (i + 1, juce::String (slotLabel (i)) + "   " + proc.getDocument().getDisplayName (i));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor).withMaximumNumColumns (2),
                     [safe = juce::Component::SafePointer<OscarEditor> (this), slot] (int r)
                     {
                         if (safe != nullptr && r > 0)
                         {
                             safe->proc.getDocument().swapSlots (slot, r - 1);
                             safe->proc.setSelectedSlot (r - 1);
                         }
                     });
}

void OscarEditor::saveInto (const juce::File& folder, const juce::String& extension, const juce::String& titleText,
                            const juce::String& messageText, const juce::String& initialName,
                            std::function<juce::Result (const juce::File&)> save)
{
    askText (titleText, messageText, initialName, tr ("Save"),
             [this, folder, extension, save = std::move (save)] (const juce::String& raw)
             {
                 saveNamed (folder, extension, raw, save);
             });
}

juce::File OscarEditor::saveNamed (const juce::File& folder, const juce::String& extension, const juce::String& rawName,
                                   const std::function<juce::Result (const juce::File&)>& save)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return {};
    if (! folder.createDirectory())
    {
        showResult (juce::Result::fail (tr ("Could not create the folder %1.", { folder.getFullPathName() })));
        return {};
    }
    auto file = folder.getChildFile (name + extension);
    if (file.exists())
        file = folder.getNonexistentChildFile (name, extension, false);
    const auto r = save (file);
    showResult (r, tr ("Saved %1 in %2.", { file.getFileName(), "Documents > OXIde > " + folder.getFileName() }));
    libraryChanged();
    return r.wasOk() ? file : juce::File();
}

juce::Result OscarEditor::exportVoiceTo (const juce::File& f)
{
    return proc.getDocument().exportVoice (proc.getSelectedSlot(), f);
}

juce::Result OscarEditor::exportSyxTo (const juce::File& f)
{
    return proc.getDocument().exportSyx (f);
}

void OscarEditor::libraryChanged()
{
    if (banksPage != nullptr)
        banksPage->rescanLibrary();
}

void OscarEditor::exportSelectedVoice()
{
    const int slot = proc.getSelectedSlot();
    auto name = proc.getDocument().getName (slot);
    if (name.isEmpty()) name = tr ("Voice %1", { juce::String (slotLabel (slot)) });
    saveInto (OscarProcessor::getVoicesFolder(), ".oscvoice", tr ("Save the selected voice"),
              tr ("Name of the voice (saved in Documents > OXIde > Voices)."), name,
              [this, slot] (const juce::File& f) { return proc.getDocument().exportVoice (slot, f); });   // the slot chosen when asked
}

void OscarEditor::importIntoSelectedVoice()
{
    const int slot = proc.getSelectedSlot();
    runFileChooser (tr ("Import a voice into voice %1", { juce::String (slotLabel (slot)) }), "*.oscvoice",
                    OscarProcessor::getVoicesFolder(), [this, slot] (const juce::File& f)
    {
        showResult (proc.getDocument().importVoice (slot, f),
                    tr ("Imported %1 into voice %2 (not yet sent to the OSCar).", { f.getFileName(), juce::String (slotLabel (slot)) }));
    });
}

void OscarEditor::loadBankFromFile()
{
    runFileChooser (tr ("Load an OSCar bank"), "*.syx;*.oscbank", OscarProcessor::getBanksFolder(), [this] (const juce::File& f)
    {
        juce::String report;
        const auto r = proc.getDocument().loadFile (f, report);
        showResult (r);
        if (r.wasOk()) proc.addLog (report);
    });
}

void OscarEditor::exportBankSyx()
{
    saveInto (OscarProcessor::getBanksFolder(), ".syx", tr ("Export as .syx"),
              tr ("Name of the .syx file (saved in Documents > OXIde > Banks). A .syx file holds the voices without their names."),
              tr ("My bank"), [this] (const juce::File& f) { return exportSyxTo (f); });
}

void OscarEditor::runFileChooser (const juce::String& titleText, const juce::String& patterns, const juce::File& folder,
                                  std::function<void (const juce::File&)> onChosen)
{
    auto dir = folder.isDirectory() ? folder : defaultFolder();
    dir.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser> (titleText, dir, patterns);

    const int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (chooserFlags, [safe = juce::Component::SafePointer<OscarEditor> (this), cb = std::move (onChosen)] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (safe != nullptr && f != juce::File())
            cb (f);
    });
}

void OscarEditor::showResult (const juce::Result& r, const juce::String& successText)
{
    if (r.failed())
    {
        proc.addLog (r.getErrorMessage());
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (tr ("Nothing was changed")).withMessage (r.getErrorMessage())
                                          .withButton ("OK").withAssociatedComponent (this), nullptr);
    }
    else if (successText.isNotEmpty())
    {
        proc.addLog (successText);
    }
}

//==============================================================================
void OscarEditor::showUpdateDialog()
{
    auto& up = proc.getUpdater();
    const auto& rel = up.getRelease();
    auto opts = juce::MessageBoxOptions().withAssociatedComponent (this);

    if (up.getState() == Updater::State::Installed)
    {
        juce::AlertWindow::showAsync (opts.withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle (tr ("Update installed")).withMessage (up.getStatusText()).withButton ("OK"), nullptr);
        return;
    }
    if (rel.version.isEmpty())
        return;

    auto notes = Updater::notesForLanguage (rel.notes, proc.getLanguage()).trim();
    if (notes.length() > 1800)
        notes = notes.substring (0, 1800) + "...";
    const bool available = up.getState() == Updater::State::Available;
   #if JUCE_MAC
    const juce::String installText = tr ("Install now");
   #else
    const juce::String installText = tr ("Open the download page");
   #endif

    auto box = opts.withIconType (juce::MessageBoxIconType::InfoIcon)
                   .withTitle ("OXIde " + rel.version + (rel.title.isNotEmpty() && rel.title != rel.version ? "  -  " + rel.title : juce::String()))
                   .withMessage ((notes.isEmpty() ? tr ("No release notes.") : notes)
                                 + (available ? "\n\n" + tr ("You have version %1. The new version replaces the plug-ins and the app on this Mac; quit and reopen your DAW afterwards.", { Updater::currentVersion() })
                                              : juce::String()));
    if (available)
        box = box.withButton (installText).withButton (tr ("Later")).withButton (tr ("Skip this version"));
    else
        box = box.withButton ("OK");

    juce::AlertWindow::showAsync (box, [safe = juce::Component::SafePointer<OscarEditor> (this), available] (int r)
    {
        if (safe == nullptr || ! available)
            return;
        auto& u = safe->proc.getUpdater();
        if (r == 1)      u.install();
        else if (r == 3) u.skipThisVersion();
    });
}

//==============================================================================
void OscarEditor::checkOutdatedCopies()
{
   #if JUCE_MAC
    static bool asked = false;   // once per session, even with several instances open
    if (asked)
        return;
    const auto old = Updater::outdatedCopies();
    if (old.isEmpty())
        return;
    asked = true;
    juce::StringArray paths;
    for (const auto& f : old)
        paths.add (f.getFullPathName());
    proc.addLog (tr ("Old copies still installed: %1", { paths.joinIntoString (", ") }));
    const auto box = juce::MessageBoxOptions().withAssociatedComponent (this).withIconType (juce::MessageBoxIconType::WarningIcon)
                         .withTitle (tr ("An older copy is still installed"))
                         .withMessage (tr ("These copies still carry the old name (OSCar Editor). They are the same plug-in as OXIde, and two copies confuse Logic:\n\n%1\n\nMove them to the Trash?",
                                           { paths.joinIntoString ("\n") }))
                         .withButton (tr ("Move to the Trash")).withButton (tr ("Later"));
    juce::AlertWindow::showAsync (box, [safe = juce::Component::SafePointer<OscarEditor> (this), old] (int r)
    {
        if (safe == nullptr || r != 1)
            return;
        juce::StringArray left;
        for (const auto& f : old)
            if (! f.moveToTrash())
                left.add (f.getFullPathName());
        safe->proc.addLog (left.isEmpty() ? tr ("The old copies are in the Trash. Restart Logic so that it forgets them.")
                                          : tr ("Couldn't move to the Trash (protected folder): %1. Drag them to the Trash in the Finder.",
                                                { left.joinIntoString (", ") }));
    });
   #endif
}

//==============================================================================
void OscarEditor::confirmThenSendVoices()
{
    auto& doc = proc.getDocument();
    if (! proc.getMidi().isOutputOpen())
    {
        proc.addLog (tr ("Choose the MIDI output connected to the OSCar's MIDI IN first (Setup page)."));
        showPage (setupPageIndex);
        return;
    }

    auto send = [safe = juce::Component::SafePointer<OscarEditor> (this)] (int r, bool remember)
    {
        if (safe == nullptr || r == 0)
            return;
        if (remember)
            safe->proc.sendConfirmedThisSession = true;
        const auto err = safe->proc.sendVoices (safe->proc.getSelectAfterSend());
        if (err.isNotEmpty())
            safe->proc.addLog (err);
    };

    auto opts = juce::MessageBoxOptions().withAssociatedComponent (this);

    switch (doc.getVoiceSync())
    {
        case BankDocument::Sync::Unknown:
            juce::AlertWindow::showAsync (opts.withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (tr ("Replace all 36 voices in the OSCar?"))
                .withMessage (tr ("The plugin doesn't know what your OSCar holds right now. The OSCar's MIDI only accepts complete banks, so all 36 voice memories will be overwritten.\n\nTo keep what is in the OSCar, cancel and dump its voices first (hold INSERT, press VOICE, then SPACE): the dump is backed up automatically.\n\nA copy of what is sent is saved in the backups (Banks page)."))
                .withButton (tr ("Replace all 36 voices")).withButton (tr ("Cancel")),
                [send] (int r) { send (r, false); });
            break;

        case BankDocument::Sync::InSync:
            juce::AlertWindow::showAsync (opts.withIconType (juce::MessageBoxIconType::InfoIcon)
                .withTitle (tr ("Nothing to send"))
                .withMessage (tr ("As far as the plugin knows, the OSCar already holds this exact bank. Send it again anyway?"))
                .withButton (tr ("Send again")).withButton (tr ("Cancel")),
                [send] (int r) { send (r, false); });
            break;

        case BankDocument::Sync::Edited:
            if (proc.sendConfirmedThisSession)
            {
                send (1, false);
                break;
            }
            juce::AlertWindow::showAsync (opts.withIconType (juce::MessageBoxIconType::QuestionIcon)
                .withTitle (tr ("Send changes to the OSCar?"))
                .withMessage (tr ("Edited voices: %1.\n\nThe whole bank is sent (the OSCar can't receive single voices); the other voices are rewritten with the content they already have. The OSCar's previous contents are backed up.\n\nIf you stored sounds on the OSCar itself since the last dump, they will be overwritten: dump first in that case.",
                                  { slotsText (doc.slotsDifferingFromOscar()) }))
                .withButton (tr ("Send")).withButton (tr ("Send, don't ask again")).withButton (tr ("Cancel")),
                [send] (int r) { send (r, r == 2); });
            break;
    }
}

void OscarEditor::confirmThenSendWaves()
{
    if (! proc.getMidi().isOutputOpen())
    {
        proc.addLog (tr ("Choose the MIDI output connected to the OSCar's MIDI IN first (Setup page)."));
        return;
    }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withAssociatedComponent (this)
        .withIconType (juce::MessageBoxIconType::QuestionIcon)
        .withTitle (tr ("Replace the user waveforms in the OSCar?"))
        .withMessage (tr ("Waveforms -8 to -12 are all rewritten. Voices that use them will sound different.\n\nA copy of what is sent is saved in the backups."))
        .withButton (tr ("Send waveforms")).withButton (tr ("Cancel")),
        [safe = juce::Component::SafePointer<OscarEditor> (this)] (int r)
        {
            if (safe == nullptr || r != 1) return;
            const auto err = safe->proc.sendWaves();
            if (err.isNotEmpty()) safe->proc.addLog (err);
        });
}

void OscarEditor::confirmThenSendSequences()
{
    if (! proc.getMidi().isOutputOpen())
    {
        proc.addLog (tr ("Choose the MIDI output connected to the OSCar's MIDI IN first (Setup page)."));
        return;
    }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withAssociatedComponent (this)
        .withIconType (juce::MessageBoxIconType::WarningIcon)
        .withTitle (tr ("Replace all sequences in the OSCar?"))
        .withMessage (tr ("The sequence backup held by the plugin replaces every sequence in the OSCar. Sequences from M2 firmware can't be loaded into older firmware."))
        .withButton (tr ("Send sequences")).withButton (tr ("Cancel")),
        [safe = juce::Component::SafePointer<OscarEditor> (this)] (int r)
        {
            if (safe == nullptr || r != 1) return;
            const auto err = safe->proc.sendSequences();
            if (err.isNotEmpty()) safe->proc.addLog (err);
        });
}
