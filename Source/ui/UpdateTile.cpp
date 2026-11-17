#include "UpdateTile.h"
#include "Lang.h"

using lang::tr;
using namespace atelier;

namespace
{
    constexpr int pad = 20;

    float textHeight (const juce::String& text, const juce::Font& font, float width)
    {
        if (text.isEmpty())
            return 0.0f;
        juce::AttributedString s;
        s.append (text, font, juce::Colours::white);
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (s, width);
        return std::ceil (tl.getHeight());
    }

    void drawWrapped (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour c,
                      juce::Rectangle<float> area)
    {
        juce::AttributedString s;
        s.append (text, font, c);
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (s, area.getWidth());
        tl.draw (g, area);
    }

    juce::Font itemFont()    { return Fonts::label (16.0f); }
    juce::Font bodyFont()    { return Fonts::label (15.5f); }
    juce::Font footFont()    { return Fonts::label (13.0f); }
    constexpr float itemVersionW = 50.0f;
}

//==============================================================================
UpdateTile::UpdateTile (Updater& u, std::function<juce::String()> lang, bool isStandalone)
    : updater (u), language (std::move (lang)), standalone (isStandalone)
{
    mainButton.fontHeight = 14.5f;
    mainButton.onClick   = [this] { if (mainAction) mainAction(); };
    secondButton.onClick = [this] { if (secondAction) secondAction(); };
    cross.onClick        = [this] { if (onDismiss) onDismiss(); };
    link.onClick         = [this] { updater.openReleasePage(); };
    skipLink.onClick     = [this] { updater.skipThisVersion(); if (onDismiss) onDismiss(); };
    skipLink.dim = true;
    cross.setTooltip (tr ("Later"));
    for (auto* c : std::initializer_list<juce::Component*> { &mainButton, &secondButton, &link, &skipLink, &cross })
        addAndMakeVisible (c);

    updater.changes.addChangeListener (this);
    refresh();
}

UpdateTile::~UpdateTile()
{
    updater.changes.removeChangeListener (this);
}

bool UpdateTile::hasSomethingToShow() const
{
    using S = Updater::State;
    const auto s = updater.getState();
    return s == S::Available || s == S::Installing || s == S::Installed || updater.installFailed();
}

void UpdateTile::refresh()
{
    using S = Updater::State;
    const auto& rel = updater.getRelease();
    const auto state = updater.getState();

    version = "OXIde " + rel.version;
    youHave = tr ("You have %1", { Updater::currentVersion() });
    items.clear();
    body.clear();
    footnote.clear();
    mainAction = secondAction = nullptr;
    busy = state == S::Installing;
    link.text = tr ("What's new, in full");
    link.setVisible (true);
    skipLink.text = tr ("Skip this version");
    skipLink.setVisible (false);
    secondButton.setVisible (true);
    mainButton.setVisible (true);

    const bool inPlace = inPlaceUpdates;

    auto dismiss = [this] { if (onDismiss) onDismiss(); };

    if (state == S::Installing)
    {
        kicker = tr ("Updating");
        body = updater.getStatusText();
        footnote = tr ("The app, the AU and the VST3 are replaced together. Keep this window open meanwhile.");
        mainButton.setVisible (false);
        link.setVisible (false);
        secondButton.setButtonText (tr ("Hide"));
        secondAction = dismiss;
    }
    else if (state == S::Installed)
    {
        kicker = tr ("Update installed");
        juce::StringArray kinds;
        for (const auto& path : updater.getInstalledPaths())
        {
            const auto f = juce::File (path);
            kinds.addIfNotAlreadyThere (f.hasFileExtension ("app") ? tr ("the app")
                                        : f.hasFileExtension ("component") ? juce::String ("AU")
                                        : f.hasFileExtension ("vst3") ? juce::String ("VST3") : f.getFileName());
        }
        body = kinds.isEmpty() ? updater.getStatusText()
                               : tr ("Installed on this Mac: %1.", { kinds.joinIntoString (", ") })
                                     + (updater.getInstallWarning().isNotEmpty() ? "\n" + updater.getInstallWarning() : juce::String());
        if (standalone && inPlace)
        {
            footnote = tr ("Relaunch: OXIde closes and opens again in its new version, your voices and settings kept.");
            mainButton.setButtonText (tr ("Relaunch OXIde"));
            mainAction = [this]
            {
                if (Updater::relaunchWhenQuit())
                {
                    if (onQuit)
                        onQuit();
                    return;
                }
                body = tr ("OXIde couldn't relaunch itself: quit it and open it again.");
                preferredHeight = layoutHeight();
                setSize (getWidth(), preferredHeight);
                repaint();
            };
            secondButton.setButtonText (tr ("Later"));
            secondAction = dismiss;
        }
        else
        {
            footnote = tr ("Your DAW loads the new AU and VST3 the next time it opens: save your project, quit it and open it again.");
            mainButton.setButtonText ("OK");
            mainAction = dismiss;
            secondButton.setVisible (false);
        }
        link.setVisible (false);
    }
    else if (updater.installFailed())
    {
        kicker = tr ("The update didn't install");
        body = updater.getStatusText();
        footnote = tr ("Nothing was changed. You can try again, or install it by hand from the download page.");
        link.text = tr ("Open the download page");
        mainButton.setButtonText (tr ("Try again"));
        mainAction = [this] { updater.install(); };
        secondButton.setButtonText (tr ("Close"));
        secondAction = dismiss;
    }
    else
    {
        kicker = tr ("New version");
        items = Updater::headlines (rel.notes, language ? language() : juce::String ("en"), Updater::currentVersion(), 3);
        if (items.empty())
            body = tr ("A new version of OXIde is out.");
        footnote = inPlace ? tr ("Updates the app, the AU and the VST3 on this Mac in one go. Your banks, voices and settings stay.")
                           : tr ("Opens the download page: install it as the first time.");
        mainButton.setButtonText (inPlace ? tr ("Update now") : tr ("Download"));
        mainAction = [this] { updater.install(); };
        secondButton.setButtonText (tr ("Later"));
        secondAction = dismiss;
        skipLink.setVisible (true);
    }

    if (busy) startTimerHz (30);
    else      stopTimer();

    preferredHeight = layoutHeight();
    if (getHeight() != preferredHeight && getWidth() > 0)
        setSize (getWidth(), preferredHeight);
    repaint();
}

int UpdateTile::layoutHeight()
{
    const float w = (float) (tileWidth - 2 * pad);
    int y = 92;

    if (! items.empty())
    {
        int h = 0;
        for (const auto& it : items)
            h += (int) textHeight (it.text, itemFont(), w - (it.version.isNotEmpty() ? itemVersionW : 0.0f)) + 6;
        itemsArea = { pad, y, (int) w, h - 6 };
        y += h - 6;
    }
    else
    {
        itemsArea = {};
    }

    if (body.isNotEmpty())
    {
        if (! items.empty())
            y += 8;
        const int h = (int) textHeight (body, bodyFont(), w);
        bodyArea = { pad, y, (int) w, h };
        y += h;
    }
    else
    {
        bodyArea = {};
    }

    if (busy)
    {
        y += 12;
        barArea = { pad, y, (int) w, 6 };
        y += 6;
    }
    else
    {
        barArea = {};
    }

    if (link.isVisible() || skipLink.isVisible())
    {
        y += 12;
        link.setBounds (shadow + pad, shadow + y, link.isVisible() ? 240 : 0, 22);
        skipLink.setBounds (shadow + tileWidth - pad - 190, shadow + y, 190, 22);
        y += 22;
    }

    if (footnote.isNotEmpty())
    {
        y += 8;
        const int h = (int) textHeight (footnote, footFont(), w);
        footArea = { pad, y, (int) w, h };
        y += h;
    }
    else
    {
        footArea = {};
    }

    y += 18;
    const int buttonH = 36;
    int x = pad;
    if (mainButton.isVisible())
    {
        const int bw = juce::jlimit (150, 230, (int) juce::GlyphArrangement::getStringWidth (Fonts::bold (mainButton.fontHeight).withExtraKerningFactor (0.20f),
                                                                                      lang::upper (mainButton.getButtonText())) + 56);
        mainButton.setFace ({ shadow + x, shadow + y, bw, buttonH });
        x += bw + 12;
    }
    if (secondButton.isVisible())
        secondButton.setBounds (shadow + x, shadow + y + 1, 128, buttonH - 1);
    y += buttonH + 20;

    cross.setBounds (shadow + tileWidth - 40, shadow + 10, 28, 28);
    return y + 2 * shadow;
}

void UpdateTile::resized()
{
    layoutHeight();
}

void UpdateTile::timerCallback()
{
    phase = std::fmod (phase + 0.018f, 1.0f);
    repaint (barArea.translated (shadow, shadow).expanded (2));
}

//==============================================================================
void UpdateTile::paint (juce::Graphics& g)
{
    const float corner = 10.0f;
    {
        juce::Path plate;
        plate.addRoundedRectangle (getLocalBounds().toFloat().reduced ((float) shadow), corner);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), shadow, { 0, 6 }).drawForPath (g, plate);
    }
    g.setOrigin (shadow, shadow);
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) tileWidth, (float) (getHeight() - 2 * shadow)).reduced (1.0f);

    // the tile: a raised plate, a copper edge, a glow along the top
    juce::ColourGradient fill (colours::panelTop.brighter (0.06f), 0.0f, r.getY(), colours::panelLow, 0.0f, r.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (r, corner);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, corner);
        g.reduceClipRegion (clip);
        juce::ColourGradient glow (colours::copper.withAlpha (0.22f), r.getCentreX(), r.getY() - 40.0f,
                                   colours::copper.withAlpha (0.0f), r.getCentreX(), r.getY() + 90.0f, true);
        g.setGradientFill (glow);
        g.fillRect (r.withHeight (100.0f));
        g.setColour (colours::copper);
        g.fillRect (r.withHeight (3.0f));
    }
    g.setColour (colours::copper.withAlpha (0.55f));
    g.drawRoundedRectangle (r, corner, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r.expanded (0.8f), corner + 0.8f, 0.8f);

    // kicker: a lit LED and the line's name
    drawLed (g, { (float) pad + 4.0f, 27.0f }, 8.0f, updater.getState() != Updater::State::Failed);
    g.setColour (updater.getState() == Updater::State::Failed ? colours::warn : colours::silk);
    g.setFont (Fonts::bold (13.0f).withExtraKerningFactor (0.12f));
    g.drawText (lang::upper (kicker), pad + 16, 18, tileWidth - 2 * pad - 60, 18, juce::Justification::centredLeft);

    // the version, large, and the one in use
    g.setColour (colours::vfd);
    g.setFont (Fonts::title (34.0f));
    g.drawText (version, pad, 40, tileWidth - 2 * pad, 38, juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (Fonts::label (14.0f));
    g.drawText (youHave, pad, 46, tileWidth - 2 * pad, 30, juce::Justification::bottomRight);

    g.setColour (colours::groupLine);
    g.fillRect ((float) pad, 84.0f, (float) (tileWidth - 2 * pad), 1.0f);

    // what it brings: one line per version
    {
        float y = (float) itemsArea.getY();
        const float w = (float) itemsArea.getWidth();
        for (const auto& it : items)
        {
            float x = (float) pad;
            if (it.version.isNotEmpty())
            {
                g.setColour (colours::vfdDim);
                g.setFont (Fonts::mono (14.0f));
                g.drawText (it.version, juce::Rectangle<float> (x, y + 1.0f, itemVersionW, 20.0f), juce::Justification::centredLeft);
                x += itemVersionW;
            }
            const float tw = w - (x - (float) pad);
            const float h = textHeight (it.text, itemFont(), tw);
            drawWrapped (g, it.text, itemFont(), colours::text, { x, y, tw, h });
            y += h + 6.0f;
        }
    }

    if (! bodyArea.isEmpty())
        drawWrapped (g, body, bodyFont(), updater.getState() == Updater::State::Failed ? colours::warn : colours::text,
                     bodyArea.toFloat());

    if (! barArea.isEmpty())
    {
        auto bar = barArea.toFloat();
        g.setColour (colours::insetLow);
        g.fillRoundedRectangle (bar, 3.0f);
        const float segW = bar.getWidth() * 0.28f;
        const float x = bar.getX() - segW + (bar.getWidth() + segW) * phase;
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (barArea);
        juce::ColourGradient seg (colours::copper.withAlpha (0.0f), x, 0.0f, colours::copperLight, x + segW, 0.0f, false);
        g.setGradientFill (seg);
        g.fillRoundedRectangle ({ x, bar.getY(), segW, bar.getHeight() }, 3.0f);
    }

    if (! footArea.isEmpty())
        drawWrapped (g, footnote, footFont(), colours::dim, footArea.toFloat());
}

//==============================================================================
void UpdateTile::Link::paint (juce::Graphics& g)
{
    const bool over = isMouseOver();
    const auto c = dim ? (over ? colours::label : colours::faint) : (over ? colours::copperLight : colours::copper);
    g.setColour (c);
    g.setFont (Fonts::label (14.5f));
    const auto just = dim ? juce::Justification::centredRight : juce::Justification::centredLeft;
    const auto shown = dim ? text : text + juce::String::fromUTF8 ("  \xe2\x80\xba");
    g.drawText (shown, getLocalBounds(), just);
    const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), shown);
    const float x0 = dim ? (float) getWidth() - tw : 0.0f;
    const float y = (float) getHeight() * 0.5f + 8.0f;
    g.setColour (c.withAlpha (over ? 0.9f : 0.45f));
    g.fillRect (x0, y, dim ? tw : tw - 12.0f, 1.0f);
}

void UpdateTile::Cross::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f).translated (0.0f, down ? 1.0f : 0.0f);
    g.setColour (highlighted ? colours::text : colours::dim);
    juce::Path p;
    p.startNewSubPath (r.getTopLeft());     p.lineTo (r.getBottomRight());
    p.startNewSubPath (r.getTopRight());    p.lineTo (r.getBottomLeft());
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
