#include "Atelier.h"

#include <cstring>

#include "BinaryData.h"
#include "Lang.h"

namespace atelier
{
//==============================================================================
// Fonts
//==============================================================================
namespace
{
    struct FontCache
    {
        static juce::Typeface::Ptr make (const char* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }

        juce::Typeface::Ptr title    = make (BinaryData::CormorantSCSemiBold_ttf, BinaryData::CormorantSCSemiBold_ttfSize);
        juce::Typeface::Ptr label    = make (BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize);
        juce::Typeface::Ptr bold     = make (BinaryData::BarlowSemiCondensedBold_ttf, BinaryData::BarlowSemiCondensedBold_ttfSize);
        juce::Typeface::Ptr mono     = make (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
    };

    juce::Font fontFrom (juce::Typeface::Ptr FontCache::* which, float h)
    {
        juce::SharedResourcePointer<FontCache> cache;
        return juce::Font (juce::FontOptions ((*cache).*which).withHeight (h));
    }

    constexpr float pi = juce::MathConstants<float>::pi;

    juce::Point<float> polar (juce::Point<float> c, float radius, float angleFromTop)
    {
        return { c.x + radius * std::sin (angleFromTop), c.y - radius * std::cos (angleFromTop) };
    }

    float knobAngle (float proportion)
    {
        return juce::degreesToRadians (-135.0f + 270.0f * juce::jlimit (0.0f, 1.0f, proportion));
    }

    void dropShadow (juce::Graphics& g, juce::Rectangle<float> circle, float offset, float alpha)
    {
        auto s = circle.translated (0.0f, offset).expanded (offset * 0.6f);
        juce::ColourGradient grad (juce::Colours::black.withAlpha (alpha), s.getCentreX(), s.getCentreY(),
                                   juce::Colours::transparentBlack, s.getCentreX(), s.getY(), true);
        g.setGradientFill (grad);
        g.fillEllipse (s);
    }

    // Conic gradient emulated with thin pie slices.
    void fillConic (juce::Graphics& g, juce::Rectangle<float> circle, const std::vector<std::pair<float, juce::Colour>>& stops,
                    float rotation = 0.0f, int slices = 96)
    {
        const auto c = circle.getCentre();
        const float r = circle.getWidth() * 0.5f;
        auto colourAt = [&stops] (float t)
        {
            for (size_t i = 1; i < stops.size(); ++i)
                if (t <= stops[i].first)
                {
                    const float span = stops[i].first - stops[i - 1].first;
                    const float k = span > 0 ? (t - stops[i - 1].first) / span : 0.0f;
                    return stops[i - 1].second.interpolatedWith (stops[i].second, k);
                }
            return stops.back().second;
        };
        for (int i = 0; i < slices; ++i)
        {
            const float a0 = rotation + 2.0f * pi * (float) i / (float) slices;
            const float a1 = rotation + 2.0f * pi * (float) (i + 1) / (float) slices + 0.01f;
            juce::Path p;
            p.addPieSegment (c.x - r, c.y - r, 2 * r, 2 * r, a0, a1, 0.0f);
            g.setColour (colourAt ((float) i / (float) slices));
            g.fillPath (p);
        }
    }

    void ticks (juce::Graphics& g, juce::Point<float> c, float rOuter, float lenMinor, float lenMajor, juce::Colour col, float thickness)
    {
        g.setColour (col);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = knobAngle ((float) i / 10.0f);
            const float len = (i == 0 || i == 5 || i == 10) ? lenMajor : lenMinor;
            g.drawLine (juce::Line<float> (polar (c, rOuter, a), polar (c, rOuter - len, a)), thickness);
        }
    }

    //==========================================================================
    // Value noise for the wood grain
    float hash (int x, int y, int seed)
    {
        auto h = (std::uint32_t) x * 374761393u + (std::uint32_t) y * 668265263u + (std::uint32_t) seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (float) (h & 0xffffffu) / 16777215.0f;
    }

    float noise (float x, float y, int seed)
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        const float fx = x - (float) xi, fy = y - (float) yi;
        const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
        const float a = hash (xi, yi, seed), b = hash (xi + 1, yi, seed);
        const float c = hash (xi, yi + 1, seed), d = hash (xi + 1, yi + 1, seed);
        return juce::jmap (sy, juce::jmap (sx, a, b), juce::jmap (sx, c, d));
    }

    float fbm (float x, float y, int seed, int octaves)
    {
        float sum = 0, amp = 0.5f, norm = 0, f = 1.0f;
        for (int i = 0; i < octaves; ++i)
        {
            sum += amp * noise (x * f, y * f, seed + i * 31);
            norm += amp;
            amp *= 0.5f;
            f *= 2.0f;
        }
        return sum / norm;
    }

    // Long-grain wood (oak/teak-like), lit from above, with a rounded outer edge and a bevel.
    juce::Image makeWood (int pw, int ph, float scale, bool left, int seed)
    {
        juce::Image img (juce::Image::RGB, juce::jmax (1, pw), juce::jmax (1, ph), false);
        juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);

        const float w = (float) pw / scale, h = (float) ph / scale;
        const float darkR = 0.36f, darkG = 0.20f, darkB = 0.09f;
        const float lightR = 0.82f, lightG = 0.56f, lightB = 0.32f;

        for (int iy = 0; iy < ph; ++iy)
        {
            const float y = (float) iy / scale;
            // long grain: almost straight, slightly slanted lines that sway gently
            const float sway = (fbm (y * 0.0025f, 3.1f, seed, 3) - 0.5f) * 5.0f + y * 0.008f;

            for (int ix = 0; ix < pw; ++ix)
            {
                const float x = (float) ix / scale;
                const float u = x + sway + (fbm (y * 0.01f, x * 0.08f, seed + 5, 2) - 0.5f) * 0.8f;

                const float bands = fbm (u * 0.22f, y * 0.0018f, seed + 11, 5);
                const float spacing = 1.0f + 0.6f * fbm (u * 0.05f, y * 0.001f, seed + 17, 2);   // uneven growth rings
                float lines = 0.5f + 0.5f * std::sin (u * 1.6f * spacing + bands * 2.5f);
                lines = lines * lines * lines * lines * lines * lines * lines * lines;

                const float t = juce::jlimit (0.0f, 1.0f, (bands - 0.28f) * 1.8f);
                float r = juce::jmap (t, darkR, lightR), gg = juce::jmap (t, darkG, lightG), b = juce::jmap (t, darkB, lightB);

                const float lineDark = 1.0f - 0.32f * lines;
                r *= lineDark; gg *= lineDark; b *= lineDark;

                const float pore = fbm (u * 1.4f, y * 0.09f, seed + 23, 2);    // short dark flecks along the grain
                if (pore < 0.32f)
                {
                    const float k = 0.82f + 0.18f * (pore / 0.32f);
                    r *= k; gg *= k; b *= k;
                }

                // shape: s = 0 at the outer edge, 1 where the cheek meets the panel
                const float s = left ? x / w : 1.0f - x / w;
                float shade = 0.55f + 0.45f * juce::jlimit (0.0f, 1.0f, s / 0.25f);
                shade += 0.20f * std::exp (-std::pow ((s - 0.88f) / 0.05f, 2.0f));      // bevel highlight
                if (s > 0.95f) shade *= 0.55f + 0.45f * (1.0f - (s - 0.95f) / 0.05f);    // shadow against the panel
                shade *= 1.0f + 0.14f * juce::jlimit (0.0f, 1.0f, 1.0f - y / (0.3f * h));   // varnish sheen at the top
                shade *= 1.0f - 0.28f * juce::jlimit (0.0f, 1.0f, (y / h - 0.8f) / 0.2f);  // darker towards the bottom

                px.setPixelColour (ix, iy, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, r * shade),
                                                                         juce::jlimit (0.0f, 1.0f, gg * shade),
                                                                         juce::jlimit (0.0f, 1.0f, b * shade), 1.0f));
            }
        }
        return img;
    }
}

juce::Font Fonts::title (float h) { return fontFrom (&FontCache::title, h); }
juce::Font Fonts::label (float h) { return fontFrom (&FontCache::label, h); }
juce::Font Fonts::bold (float h)  { return fontFrom (&FontCache::bold, h); }
juce::Font Fonts::mono (float h)  { return fontFrom (&FontCache::mono, h); }

//==============================================================================
// Drawing helpers
//==============================================================================
void drawWoodCheek (juce::Graphics& g, juce::Rectangle<float> area, bool leftSide, int seed)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    g.drawImage (makeWood (juce::roundToInt (area.getWidth() * scale), juce::roundToInt (area.getHeight() * scale), scale, leftSide, seed),
                 area);
}

//==============================================================================
// WearLayer
//==============================================================================
void WearLayer::setup (int lvl, std::function<juce::Colour (juce::Point<float>)> bg, std::vector<juce::Path> clear,
                       std::vector<Spot> handled, std::vector<juce::Rectangle<float>> plateAreas)
{
    level = lvl;
    background = std::move (bg);
    keepClear = std::move (clear);
    spots = std::move (handled);
    plates = std::move (plateAreas);
    cache = {};
    setVisible (level > 0);
    repaint();
}

void WearLayer::paint (juce::Graphics& g)
{
    if (level <= 0)
        return;
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int pw = juce::roundToInt ((float) getWidth() * scale), ph = juce::roundToInt ((float) getHeight() * scale);
    if (cache.getWidth() != pw || cache.getHeight() != ph)
        cache = render (pw, ph, scale);
    g.drawImage (cache, getLocalBounds().toFloat());
}

juce::Image WearLayer::render (int pw, int ph, float scale) const
{
    struct Amounts { float fadeFrom, fadeMax, near, speck, scratches, scratchAlpha, polish, grime, rubs, chips, dust; };
    static constexpr Amounts byLevel[] = {
        { 0.585f, 0.48f, 0.45f, 0.885f,  45.0f, 0.075f, 0.040f, 0.12f, 10.0f,  4.0f, 160.0f },   // 1: discreet, ink thinned here and there
        { 0.555f, 0.62f, 0.65f, 0.855f,  80.0f, 0.095f, 0.055f, 0.17f, 18.0f, 10.0f, 300.0f },   // 2: patina of a studio instrument
        { 0.525f, 0.76f, 0.85f, 0.825f, 120.0f, 0.115f, 0.070f, 0.22f, 28.0f, 22.0f, 460.0f } }; // 3: years of use and gigs
    const auto& a = byLevel[juce::jlimit (1, 3, level) - 1];
    const int seed = 7;

    juce::Image img (juce::Image::ARGB, juce::jmax (1, pw), juce::jmax (1, ph), true);

    // 1. ink rubbed away: the panel's own colour laid over it, in soft patches and fine specks
    {
        juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
        for (int iy = 0; iy < ph; ++iy)
        {
            const float y = (float) iy / scale;
            for (int ix = 0; ix < pw; ++ix)
            {
                const float x = (float) ix / scale;
                float near = 0.0f;
                for (const auto& s : spots)
                {
                    const float d = s.centre.getDistanceFrom ({ x, y }) / s.radius;
                    if (d < 1.0f)
                        near = juce::jmax (near, (1.0f - d) * (1.0f - d));
                }
                const float patch = fbm (x * 0.021f, y * 0.021f, seed, 3);
                float fade = juce::jlimit (0.0f, 1.0f, (patch - a.fadeFrom + near * a.near * 0.25f) * 6.0f) * a.fadeMax;
                const float grain = fbm (x * 0.75f, y * 0.75f, seed + 101, 2);   // little flakes of ink gone
                if (grain > a.speck - near * 0.05f - juce::jmax (0.0f, patch - 0.5f) * 0.25f)
                    fade = juce::jmax (fade, 0.9f);
                fade *= 0.75f + 0.25f * noise (x * 0.35f, y * 0.35f, seed + 57);   // never even
                if (fade > 0.01f)
                    px.setPixelColour (ix, iy, background ({ x, y }).withAlpha (juce::jmin (1.0f, fade)));
            }
        }
    }

    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));
    juce::Random rnd (seed * 31 + level);
    const juce::Rectangle<float> area (0.0f, 0.0f, (float) getWidth(), (float) getHeight());

    // 2. polish where hands go: a faint sheen
    for (const auto& s : spots)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (a.polish), s.centre.x, s.centre.y,
                                                 juce::Colours::white.withAlpha (0.0f), s.centre.x + s.radius * 1.2f, s.centre.y, true));
        g.fillEllipse (juce::Rectangle<float> (s.radius * 2.4f, s.radius * 2.0f).withCentre (s.centre));
    }

    // 3. grime settled in the corners of each column, and the edges rubbed lighter in places
    for (const auto& p : plates)
    {
        for (const auto& corner : { p.getBottomLeft(), p.getBottomRight(), p.getTopLeft(), p.getTopRight() })
        {
            const float r = 46.0f + rnd.nextFloat() * 40.0f, k = corner.y > p.getCentreY() ? 1.0f : 0.55f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d0b08).withAlpha (a.grime * k), corner.x, corner.y,
                                                     juce::Colours::transparentBlack, corner.x + r, corner.y, true));
            g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (corner));
        }
        for (int i = 0; i < (int) a.rubs / 2; ++i)
        {
            const bool horizontal = rnd.nextBool();
            const float len = 12.0f + rnd.nextFloat() * 50.0f;
            g.setColour (juce::Colours::white.withAlpha (0.035f + rnd.nextFloat() * 0.04f));
            if (horizontal)
            {
                const float x = p.getX() + 10.0f + rnd.nextFloat() * (p.getWidth() - 20.0f - len);
                const float y = rnd.nextBool() ? p.getY() + 0.5f : p.getBottom() - 1.5f;
                g.fillRect (x, y, len, 1.0f);
            }
            else
            {
                const float y = p.getY() + 10.0f + rnd.nextFloat() * (p.getHeight() - 20.0f - len);
                const float x = rnd.nextBool() ? p.getX() + 0.5f : p.getRight() - 1.5f;
                g.fillRect (x, y, 1.0f, len);
            }
        }
        for (int i = 0; i < (int) a.chips / 3; ++i)   // tiny chips of paint on the edges, bare metal underneath
        {
            const auto e = rnd.nextBool() ? juce::Point<float> (p.getX() + rnd.nextFloat() * p.getWidth(), rnd.nextBool() ? p.getY() : p.getBottom())
                                          : juce::Point<float> (rnd.nextBool() ? p.getX() : p.getRight(), p.getY() + rnd.nextFloat() * p.getHeight());
            g.setColour (juce::Colour (0xff8a8d92).withAlpha (0.18f + rnd.nextFloat() * 0.18f));
            g.fillEllipse (juce::Rectangle<float> (1.2f + rnd.nextFloat() * 2.2f, 1.0f + rnd.nextFloat() * 1.6f).withCentre (e));
        }
    }

    // 4. scratches: fine, mostly light, a few in little bunches near the handled controls
    for (int i = 0; i < (int) a.scratches; ++i)
    {
        juce::Point<float> start;
        if (! spots.empty() && rnd.nextFloat() < 0.35f)
        {
            const auto& s = spots[(size_t) rnd.nextInt ((int) spots.size())];
            start = s.centre + juce::Point<float> ((rnd.nextFloat() - 0.5f) * s.radius * 2.2f, (rnd.nextFloat() - 0.5f) * s.radius * 2.2f);
        }
        else
        {
            start = { rnd.nextFloat() * area.getWidth(), rnd.nextFloat() * area.getHeight() };
        }
        const bool longOne = rnd.nextFloat() < 0.08f;
        const float angle = rnd.nextFloat() * juce::MathConstants<float>::pi,
                    len = longOne ? 60.0f + rnd.nextFloat() * 90.0f : 5.0f + std::pow (rnd.nextFloat(), 2.0f) * 46.0f;
        const auto end = start + juce::Point<float> (std::cos (angle), std::sin (angle)) * len;
        juce::Path p;
        p.startNewSubPath (start);
        p.quadraticTo ((start + end) * 0.5f + juce::Point<float> ((rnd.nextFloat() - 0.5f) * 3.0f, (rnd.nextFloat() - 0.5f) * 3.0f), end);
        const bool dark = rnd.nextFloat() < 0.25f;
        g.setColour (dark ? juce::Colours::black.withAlpha (0.28f) : juce::Colours::white.withAlpha (a.scratchAlpha * (0.5f + rnd.nextFloat() * 0.7f)));
        g.strokePath (p, juce::PathStrokeType (0.55f + rnd.nextFloat() * 0.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 5. dust: specks of light grey, a few bigger ones
    for (int i = 0; i < (int) a.dust; ++i)
    {
        const float d = rnd.nextFloat() < 0.1f ? 1.4f + rnd.nextFloat() * 1.2f : 0.6f + rnd.nextFloat() * 0.7f;
        g.setColour (juce::Colour (0xffb9b4ab).withAlpha (0.10f + rnd.nextFloat() * 0.16f));
        g.fillEllipse (juce::Rectangle<float> (d, d * (0.7f + rnd.nextFloat() * 0.6f)).withCentre ({ rnd.nextFloat() * area.getWidth(), rnd.nextFloat() * area.getHeight() }));
    }

    // 6. nothing on the controls themselves
    juce::Image clear (juce::Image::SingleChannel, juce::jmax (1, pw), juce::jmax (1, ph), true);
    {
        juce::Graphics c (clear);
        c.addTransform (juce::AffineTransform::scale (scale));
        c.setColour (juce::Colours::white);
        for (const auto& p : keepClear)
            c.fillPath (p);
    }
    juce::Image::BitmapData out (img, juce::Image::BitmapData::readWrite), mask (clear, juce::Image::BitmapData::readOnly);
    for (int iy = 0; iy < ph; ++iy)
        for (int ix = 0; ix < pw; ++ix)
        {
            const auto m = *mask.getPixelPointer (ix, iy);
            if (m != 0)
            {
                auto* pixel = (juce::PixelARGB*) out.getPixelPointer (ix, iy);
                pixel->multiplyAlpha (255 - m);
            }
        }
    return img;
}

void WoodCheek::paint (juce::Graphics& g)
{
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int pw = juce::roundToInt ((float) getWidth() * scale), ph = juce::roundToInt ((float) getHeight() * scale);
    if (cache.getWidth() != pw || cache.getHeight() != ph)
        cache = makeWood (pw, ph, scale, isLeft, grainSeed);
    g.drawImage (cache, getLocalBounds().toFloat());
}

void drawVents (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff2f3236));
    g.fillRoundedRectangle (area, 3.0f);
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    for (float y = area.getY(); y < area.getBottom(); y += 6.0f)
    {
        g.setColour (juce::Colour (0xff08090a));
        g.fillRect (area.getX(), y, area.getWidth(), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (area.getX(), y + 3.0f, area.getWidth(), 1.0f);
    }
    g.restoreState();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (area, 3.0f, 1.0f);
}

void drawInset (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setGradientFill (juce::ColourGradient (colours::inset, area.getX(), area.getY(), colours::insetLow, area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
}

void drawBracketLabel (juce::Graphics& g, juce::Rectangle<float> row, const juce::String& text)
{
    const auto font = Fonts::bold (13.0f);
    const float tw = juce::GlyphArrangement::getStringWidth (font, text);
    const float cx = row.getCentreX();
    const float y0 = row.getY(), y1 = row.getY() + 8.0f, r = 8.0f;
    const float xl = row.getX() + 16.0f, xr = row.getRight() - 16.0f;
    const float gapL = cx - tw * 0.5f - 8.0f, gapR = cx + tw * 0.5f + 8.0f;

    juce::Path p;
    p.startNewSubPath (xl, y0);
    p.lineTo (xl, y1 - r);
    p.quadraticTo (xl, y1, xl + r, y1);
    p.lineTo (gapL, y1);
    p.startNewSubPath (gapR, y1);
    p.lineTo (xr - r, y1);
    p.quadraticTo (xr, y1, xr, y1 - r);
    p.lineTo (xr, y0);
    g.setColour (colours::faint);
    g.strokePath (p, juce::PathStrokeType (1.0f));

    g.setColour (juce::Colour (0xffdcdee1));
    g.setFont (font);
    g.drawText (text, juce::Rectangle<float> (gapL, y1 - 8.0f, gapR - gapL, 16.0f), juce::Justification::centred);
}

void drawGroup (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& bracketLabel)
{
    juce::Path frame;
    frame.addRoundedRectangle (area.reduced (0.5f), 8.0f);
    juce::Path dashed;
    const float dashes[] = { 3.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, frame, dashes, 2);
    g.setColour (colours::groupLine);
    g.fillPath (dashed);

    if (bracketLabel.isNotEmpty())
        drawBracketLabel (g, area.withTop (area.getBottom() - 22.0f).withHeight (18.0f), bracketLabel);
}

void drawDivider (juce::Graphics& g, float y, float x0, float x1, const juce::String& label)
{
    // same ink as the bracket labels: a fine line, the label in a gap in the middle, small end ticks
    const auto font = Fonts::bold (12.0f).withExtraKerningFactor (0.04f);
    const float tw = label.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, label);
    const float cx = (x0 + x1) * 0.5f, gapL = cx - tw * 0.5f - 8.0f, gapR = cx + tw * 0.5f + 8.0f;
    g.setColour (colours::faint.withAlpha (0.8f));
    if (label.isEmpty())
        g.fillRect (x0, y, x1 - x0, 1.0f);
    else
    {
        g.fillRect (x0, y, gapL - x0, 1.0f);
        g.fillRect (gapR, y, x1 - gapR, 1.0f);
    }
    g.fillRect (x0, y - 3.0f, 1.0f, 4.0f);
    g.fillRect (x1 - 1.0f, y - 3.0f, 1.0f, 4.0f);
    if (label.isNotEmpty())
    {
        g.setColour (juce::Colour (0xffc9ccd0));
        g.setFont (font);
        g.drawText (label, juce::Rectangle<float> (gapL, y - 8.0f, gapR - gapL, 16.0f), juce::Justification::centred, false);
    }
}

void drawControlLabel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, const juce::Font& font,
                       LabelStyle style, juce::Colour plainColour, int maxLines)
{
    g.setFont (font);
    if (style == LabelStyle::Plain)
    {
        g.setColour (plainColour);
        g.drawFittedText (text, area, juce::Justification::centredTop, maxLines, 0.8f);
        return;
    }
    const float tw = juce::jmin ((float) area.getWidth() - 2.0f, juce::GlyphArrangement::getStringWidth (font, text) + 10.0f);
    auto block = juce::Rectangle<float> (tw, font.getHeight() + 3.0f).withCentre ({ (float) area.getCentreX(), (float) area.getY() + (font.getHeight() + 3.0f) * 0.5f });
    g.setColour (colours::silk);
    g.drawRoundedRectangle (block.reduced (0.5f), 2.0f, 1.0f);
    g.setColour (plainColour.brighter (0.2f));
    g.drawFittedText (text, block.reduced (4.0f, 0.0f).translated (0.0f, 0.5f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float d, bool on)
{
    auto r = juce::Rectangle<float> (d, d).withCentre (c);
    if (on)
    {
        auto glow = r.expanded (d * 0.9f);
        g.setGradientFill (juce::ColourGradient (colours::ledOn.withAlpha (0.55f), c.x, c.y, colours::ledOn.withAlpha (0.0f),
                                                 c.x, glow.getY(), true));
        g.fillEllipse (glow);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffb2a6), c.x - d * 0.15f, c.y - d * 0.2f,
                                                 juce::Colour (0xffc81e10), c.x, r.getBottom(), true));
        g.fillEllipse (r);
    }
    else
    {
        g.setColour (colours::ledOff);
        g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (r.reduced (0.5f), 1.0f);
    }
}

void drawVfdWindow (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (area, 4.0f);
    auto inner = area.reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d0d06), inner.getX(), inner.getY(),
                                             juce::Colour (0xff120703), inner.getX(), inner.getBottom(), false));
    g.fillRoundedRectangle (inner, 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f), inner.getX(), inner.getY(),
                                             juce::Colours::transparentBlack, inner.getX(), inner.getY() + 5.0f, false));
    g.fillRoundedRectangle (inner, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine (juce::roundToInt (area.getBottom()), area.getX() + 2.0f, area.getRight() - 2.0f);
}

void drawSegments (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour lit, bool glow)
{
    const int n = juce::jmax (1, text.length());
    const float cell = area.getWidth() / (float) n, h = area.getHeight();
    const float w = juce::jmin (cell * 0.78f, h * 0.56f), t = h * 0.15f, gap = t * 0.22f, skew = 0.09f;
    juce::Path on, off;
    for (int i = 0; i < n; ++i)
    {
        const juce::juce_wchar ch = text[i];
        const char* segs = "";
        switch (ch)
        {
            case '0': segs = "abcdef"; break;   case '1': segs = "bc"; break;      case '2': segs = "abged"; break;
            case '3': segs = "abgcd"; break;    case '4': segs = "fgbc"; break;    case '5': segs = "afgcd"; break;
            case '6': segs = "afgedc"; break;   case '7': segs = "abc"; break;     case '8': segs = "abcdefg"; break;
            case '9': segs = "abcdfg"; break;   case '-': segs = "g"; break;       default: break;
        }
        const float ox = area.getX() + cell * (float) i + (cell - w) * 0.5f, oy = area.getY();
        auto pt = [&] (float x, float y) { return juce::Point<float> (ox + x + (h - y) * skew, oy + y); };
        auto seg = [&] (char name)
        {
            juce::Path p;
            auto horiz = [&] (float x1, float x2, float y)
            {
                p.startNewSubPath (pt (x1, y)); p.lineTo (pt (x1 + t * 0.5f, y - t * 0.5f)); p.lineTo (pt (x2 - t * 0.5f, y - t * 0.5f));
                p.lineTo (pt (x2, y)); p.lineTo (pt (x2 - t * 0.5f, y + t * 0.5f)); p.lineTo (pt (x1 + t * 0.5f, y + t * 0.5f)); p.closeSubPath();
            };
            auto vert = [&] (float x, float y1, float y2)
            {
                p.startNewSubPath (pt (x, y1)); p.lineTo (pt (x + t * 0.5f, y1 + t * 0.5f)); p.lineTo (pt (x + t * 0.5f, y2 - t * 0.5f));
                p.lineTo (pt (x, y2)); p.lineTo (pt (x - t * 0.5f, y2 - t * 0.5f)); p.lineTo (pt (x - t * 0.5f, y1 + t * 0.5f)); p.closeSubPath();
            };
            const float l = t * 0.5f, r = w - t * 0.5f;
            switch (name)
            {
                case 'a': horiz (l + gap, r - gap, t * 0.5f); break;
                case 'g': horiz (l + gap, r - gap, h * 0.5f); break;
                case 'd': horiz (l + gap, r - gap, h - t * 0.5f); break;
                case 'f': vert (l, t * 0.5f + gap, h * 0.5f - gap); break;
                case 'b': vert (r, t * 0.5f + gap, h * 0.5f - gap); break;
                case 'e': vert (l, h * 0.5f + gap, h - t * 0.5f - gap); break;
                case 'c': vert (r, h * 0.5f + gap, h - t * 0.5f - gap); break;
                default: break;
            }
            return p;
        };
        for (char name : { 'a', 'b', 'c', 'd', 'e', 'f', 'g' })
            (std::strchr (segs, name) != nullptr ? on : off).addPath (seg (name));
    }
    g.setColour (lit.withAlpha (0.05f));
    g.fillPath (off);
    if (glow)
    {
        g.setColour (lit.withAlpha (0.28f));
        g.strokePath (on, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (lit);
    g.fillPath (on);
}

void drawSegmentReadout (juce::Graphics& g, juce::Rectangle<float> window, const juce::String& text, int digits, bool bright)
{
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (window, 3.0f);
    auto inner = window.reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a0b05), inner.getX(), inner.getY(), juce::Colour (0xff100603), inner.getX(), inner.getBottom(), false));
    g.fillRoundedRectangle (inner, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (juce::roundToInt (window.getBottom()), window.getX() + 2.0f, window.getRight() - 2.0f);
    const auto padded = text.length() >= digits ? text : juce::String::repeatedString (" ", digits - text.length()) + text;
    const float digitH = inner.getHeight() * 0.62f;
    drawSegments (g, inner.reduced (inner.getHeight() * 0.22f, 0.0f).withSizeKeepingCentre (inner.getWidth() - inner.getHeight() * 0.44f, digitH),
                  padded, bright ? colours::vfd : colours::vfd.withAlpha (0.82f), bright);
}

float glyphWidth (const juce::String& legend, float h)
{
    if (legend == "~saw")   return h * 0.75f;
    if (legend == "~sqr")   return h * 1.7f;
    if (legend == "~pulse") return h * 1.7f;
    return 0.0f;
}

bool drawGlyph (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& legend, float stroke)
{
    const float w = glyphWidth (legend, box.getHeight());
    if (w <= 0.0f)
        return false;
    const auto r = juce::Rectangle<float> (w, box.getHeight() * 0.8f).withCentre (box.getCentre());
    const float x = r.getX(), y = r.getY(), b = r.getBottom(), rw = r.getWidth();
    juce::Path p;
    if (legend == "~saw")            // a ramp up, then straight down
    {
        p.startNewSubPath (x, b); p.lineTo (x + rw, y); p.lineTo (x + rw, b);
    }
    else                             // square: half and half; pulse: a narrow high part
    {
        const float hi = legend == "~sqr" ? 0.5f : 0.3f;
        p.startNewSubPath (x, b); p.lineTo (x, y); p.lineTo (x + rw * hi, y); p.lineTo (x + rw * hi, b);
        p.lineTo (x + rw, b); p.lineTo (x + rw, y);
    }
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    return true;
}

void drawHardButton (juce::Graphics& g, juce::Rectangle<float> a, bool down, bool highlighted)
{
    if (! down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (a.translated (0.0f, 2.5f), 3.0f);
    }
    auto top = down ? juce::Colour (0xff2b2d31) : juce::Colour (0xff3d4045);
    auto bottom = down ? juce::Colour (0xff17181a) : juce::Colour (0xff1d1e21);
    if (highlighted && ! down) top = top.brighter (0.12f);
    g.setGradientFill (juce::ColourGradient (top, a.getX(), a.getY(), bottom, a.getX(), a.getBottom(), false));
    g.fillRoundedRectangle (a, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.04f : 0.12f));
    g.drawHorizontalLine (juce::roundToInt (a.getY() + 1.0f), a.getX() + 2.0f, a.getRight() - 2.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (a.reduced (0.5f), 3.0f, 1.0f);
}

void drawCopper (juce::Graphics& g, juce::Rectangle<float> a, float corner, bool lit, bool down)
{
    const auto c = a.getCentre();
    if (lit)
    {
        // glow
        auto glow = a.expanded (10.0f);
        g.setGradientFill (juce::ColourGradient (colours::copper.withAlpha (down ? 0.25f : 0.38f), c.x, c.y,
                                                 colours::copper.withAlpha (0.0f), c.x, glow.getY(), true));
        g.fillRoundedRectangle (glow, corner + 10.0f);
        juce::ColourGradient grad (colours::copperLight.brighter (down ? 0.0f : 0.05f), c.x, c.y - a.getHeight() * 0.05f,
                                   colours::copperDark, c.x, a.getBottom() + a.getHeight() * 0.25f, true);
        grad.addColour (0.45, colours::copper);
        g.setGradientFill (grad);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b2a21), c.x, c.y, juce::Colour (0xff150d08),
                                                 c.x, a.getBottom() + a.getHeight() * 0.2f, true));
    }
    g.fillRoundedRectangle (a, corner);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.35f : 0.06f));
    g.drawHorizontalLine (juce::roundToInt (a.getY() + 1.0f), a.getX() + corner, a.getRight() - corner);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.0f);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, KnobStyle style)
{
    const auto c = area.getCentre();
    const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const float angle = knobAngle (proportion);

    if (style == KnobStyle::Knurled)
    {
        auto outer = juce::Rectangle<float> (2 * R, 2 * R).withCentre (c);
        dropShadow (g, outer, R * 0.22f, 0.7f);
        // knurled skirt: alternating ridges
        const int ridges = 36;
        for (int i = 0; i < ridges; ++i)
        {
            juce::Path p;
            const float a0 = 2.0f * pi * (float) i / (float) ridges;
            p.addPieSegment (outer, a0, a0 + 2.0f * pi / (float) ridges + 0.005f, 0.0f);
            g.setColour ((i % 2 == 0) ? juce::Colour (0xff303135) : juce::Colour (0xff0b0b0c));
            g.fillPath (p);
        }
        g.setColour (juce::Colours::black);
        g.drawEllipse (outer.reduced (0.5f), 1.0f);
        // top face
        auto face = outer.reduced (R * 0.26f);
        juce::ColourGradient grad (juce::Colour (0xff55585e), face.getX() + face.getWidth() * 0.4f, face.getY() + face.getHeight() * 0.3f,
                                   juce::Colour (0xff0f1011), face.getRight(), face.getBottom(), true);
        grad.addColour (0.55, juce::Colour (0xff1c1d1f));
        g.setGradientFill (grad);
        g.fillEllipse (face);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawEllipse (face.reduced (0.5f), 0.8f);
        // pointer
        juce::Path ptr;
        ptr.addRoundedRectangle (-R * 0.055f, -R * 0.68f, R * 0.11f, R * 0.50f, R * 0.05f);
        g.setColour (juce::Colour (0xfff2f2f2));
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
    }
    else if (style == KnobStyle::Chrome)
    {
        auto cap = juce::Rectangle<float> (R * 1.62f, R * 1.62f).withCentre (c);
        dropShadow (g, cap, R * 0.18f, 0.75f);
        fillConic (g, cap, { { 0.0f, juce::Colour (0xfff4f4f4) }, { 0.17f, juce::Colour (0xff9a9ca0) },
                             { 0.33f, juce::Colour (0xffe6e7e9) }, { 0.5f, juce::Colour (0xff7d8085) },
                             { 0.67f, juce::Colour (0xfff0f0f0) }, { 0.83f, juce::Colour (0xff8d9094) },
                             { 1.0f, juce::Colour (0xfff4f4f4) } }, 0.35f);
        g.setColour (juce::Colour (0xff111111));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);
        auto face = juce::Rectangle<float> (R * 1.08f, R * 1.08f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, face.getX() + face.getWidth() * 0.4f, face.getY() + face.getHeight() * 0.35f,
                                                 juce::Colour (0xffa3a6aa), face.getRight(), face.getBottom(), true));
        g.fillEllipse (face);
        g.setColour (juce::Colour (0xff1a1a1a));
        g.drawLine (juce::Line<float> (polar (c, R * 0.36f, angle), polar (c, R * 0.76f, angle)), juce::jmax (1.6f, R * 0.08f));
    }
    else // Brass
    {
        ticks (g, c, R - 1.0f, R * 0.08f, R * 0.11f, juce::Colour (0xffc9cbcf), 1.6f);
        auto ring = juce::Rectangle<float> (R * 1.6f, R * 1.6f).withCentre (c);
        dropShadow (g, ring, R * 0.12f, 0.8f);
        for (int i = 0; i < 8; ++i)   // grip lobes
        {
            juce::Path p;
            const float a0 = 2.0f * pi * (float) i / 8.0f;
            p.addPieSegment (ring, a0, a0 + 2.0f * pi * 0.8f / 8.0f, 0.0f);
            g.setColour (juce::Colour (0xff1d1e21));
            g.fillPath (p);
            juce::Path n;
            n.addPieSegment (ring, a0 + 2.0f * pi * 0.8f / 8.0f, a0 + 2.0f * pi / 8.0f + 0.004f, 0.0f);
            g.setColour (juce::Colour (0xff070708));
            g.fillPath (n);
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (ring.reduced (1.0f), 1.0f);
        auto mid = juce::Rectangle<float> (R * 1.23f, R * 1.23f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3f43), mid.getX() + mid.getWidth() * 0.4f, mid.getY() + mid.getHeight() * 0.3f,
                                                 juce::Colour (0xff141517), mid.getRight(), mid.getBottom(), true));
        g.fillEllipse (mid);
        g.setColour (juce::Colours::black);
        g.drawEllipse (mid, 1.0f);
        auto cap = juce::Rectangle<float> (R * 0.77f, R * 0.77f).withCentre (c);
        std::vector<std::pair<float, juce::Colour>> brushed;
        for (int i = 0; i <= 120; ++i)
            brushed.emplace_back ((float) i / 120.0f, (i % 2 == 0) ? juce::Colour (0xffdcc58d) : juce::Colour (0xffb49a5c));
        fillConic (g, cap, brushed, 0.0f, 120);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.6f), cap.getX() + cap.getWidth() * 0.38f,
                                                 cap.getY() + cap.getHeight() * 0.32f, juce::Colours::white.withAlpha (0.0f),
                                                 cap.getX() + cap.getWidth() * 0.38f, cap.getBottom() - cap.getHeight() * 0.1f, true));
        g.fillEllipse (cap);
        g.setColour (juce::Colour (0xff6d5a2c));
        g.drawEllipse (cap, 1.0f);
        juce::Path ptr;
        ptr.addRoundedRectangle (-R * 0.03f, -R * 0.78f, R * 0.06f, R * 0.19f, R * 0.03f);
        g.setColour (juce::Colour (0xfff4f4f4));
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
    }
}

void drawSwitchKnob (juce::Graphics& g, juce::Point<float> c, float R, float angle)
{
    // round knurled base with a bar on top: a pointer knob, so switches read apart from knobs
    auto base = juce::Rectangle<float> (2 * R, 2 * R).withCentre (c);
    dropShadow (g, base, R * 0.24f, 0.7f);
    const int ridges = 28;
    for (int i = 0; i < ridges; ++i)
    {
        juce::Path p;
        const float a0 = 2.0f * pi * (float) i / (float) ridges;
        p.addPieSegment (base, a0, a0 + 2.0f * pi / (float) ridges + 0.005f, 0.0f);
        g.setColour ((i % 2 == 0) ? juce::Colour (0xff2c2d31) : juce::Colour (0xff0c0c0d));
        g.fillPath (p);
    }
    g.setColour (juce::Colours::black);
    g.drawEllipse (base.reduced (0.5f), 1.0f);
    auto face = base.reduced (R * 0.2f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3c40), face.getX() + face.getWidth() * 0.4f, face.getY() + face.getHeight() * 0.3f,
                                             juce::Colour (0xff111214), face.getRight(), face.getBottom(), true));
    g.fillEllipse (face);

    const auto t = juce::AffineTransform::rotation (angle).translated (c);
    juce::Path bar;
    bar.startNewSubPath (-R * 0.16f, -R * 1.02f);
    bar.lineTo (R * 0.16f, -R * 1.02f);
    bar.lineTo (R * 0.34f, R * 0.62f);
    bar.quadraticTo (0.0f, R * 0.86f, -R * 0.34f, R * 0.62f);
    bar.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillPath (bar, t.translated (0.0f, R * 0.12f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4c51), c.x - R * 0.3f, c.y - R * 0.6f,
                                             juce::Colour (0xff17181a), c.x + R * 0.3f, c.y + R * 0.7f, false));
    g.fillPath (bar, t);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.strokePath (bar, juce::PathStrokeType (0.8f), t);
    juce::Path line;
    line.addRoundedRectangle (-R * 0.05f, -R * 0.92f, R * 0.1f, R * 0.62f, R * 0.04f);
    g.setColour (juce::Colour (0xfff2f2f2));
    g.fillPath (line, t);
}

//==============================================================================
// LookAndFeel
//==============================================================================
Look::Look()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1f2124));
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::copper.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff1e0b03));
    setColour (juce::PopupMenu::headerTextColourId, colours::vfd);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff222428));
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, colours::vfdBg);
    setColour (juce::TextEditor::textColourId, colours::vfd);
    setColour (juce::TextEditor::highlightColourId, colours::copper.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::black);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::copper);
    setColour (juce::CaretComponent::caretColourId, colours::vfd);
    setColour (juce::Label::textColourId, colours::label);
    setColour (juce::ComboBox::backgroundColourId, colours::vfdBg);
    setColour (juce::ComboBox::textColourId, colours::vfd);
    setColour (juce::ComboBox::arrowColourId, colours::label);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::black);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::text);
    setColour (juce::ToggleButton::textColourId, colours::label);
    setColour (juce::ToggleButton::tickColourId, colours::ledOn);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff4a4d52));
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff111214));
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::copper.withAlpha (0.6f));
    setColour (juce::ResizableWindow::backgroundColourId, colours::panel);
    setColour (juce::DocumentWindow::backgroundColourId, colours::panel);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s)
{
    if ((bool) s.getProperties().getWithDefault ("vfdNumber", false))
        return;   // VfdNumber paints its own window
    const auto style = (KnobStyle) (int) s.getProperties().getWithDefault ("knobStyle", 0);
    drawKnob (g, juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h), pos, style);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearHorizontal && (bool) s.getProperties().getWithDefault ("crossfader", false))
    {
        // a long crossfader: printed scale above and below, a chunky cap with grip ridges and an orange line
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float cy = area.getCentreY();
        for (int i = 0; i <= 20; ++i)
        {
            const float tx = area.getX() + area.getWidth() * (float) i / 20.0f;
            const bool major = i % 5 == 0;
            g.setColour (major ? colours::silk : colours::silkDim);
            const float len = i == 10 ? 12.0f : (major ? 9.0f : 5.0f);
            g.fillRect (tx - 0.5f, cy - 14.0f - len, major ? 1.5f : 1.0f, len);
            g.fillRect (tx - 0.5f, cy + 14.0f, major ? 1.5f : 1.0f, len);
        }
        auto slot = juce::Rectangle<float> (area.getX() - 26.0f, cy - 4.0f, area.getWidth() + 52.0f, 8.0f);
        g.setColour (juce::Colour (0xff050506));
        g.fillRoundedRectangle (slot, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (juce::roundToInt (slot.getBottom() + 0.5f), slot.getX() + 3.0f, slot.getRight() - 3.0f);

        auto cap = juce::Rectangle<float> (50.0f, 36.0f).withCentre ({ pos, cy });
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (cap.translated (0.0f, 3.5f), 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff55585e), cap.getX(), cap.getY(), juce::Colour (0xff1b1c1f), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (cap.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawHorizontalLine (juce::roundToInt (cap.getY() + 1.5f), cap.getX() + 3.0f, cap.getRight() - 3.0f);
        for (int i = -3; i <= 3; ++i)   // grip ridges
        {
            if (i == 0) continue;
            const float rx = cap.getCentreX() + (float) i * 5.0f;
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (rx - 0.5f, cap.getY() + 6.0f, 1.0f, cap.getHeight() - 12.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (rx + 0.5f, cap.getY() + 6.0f, 1.0f, cap.getHeight() - 12.0f);
        }
        g.setColour (juce::Colour (0xffff6a3d));
        g.fillRect (cap.getCentreX() - 1.0f, cap.getY() + 4.0f, 2.0f, cap.getHeight() - 8.0f);
        return;
    }
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }

    // The area is already inset by the thumb radius (7): 'pos' (the cap centre) moves within it.
    juce::ignoreUnused (s);
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cx = area.getCentreX() + 3.0f;
    const float capH = 14.0f, capW = 24.0f;

    // scale ticks on the left
    g.setColour (juce::Colour (0xff6b6e73));
    for (int i = 0; i <= 10; ++i)
    {
        const float ty = area.getY() + area.getHeight() * (float) i / 10.0f;
        g.fillRect (area.getX() + 2.0f, ty, (i % 5 == 0) ? 8.0f : 5.0f, 1.0f);
    }

    // slot
    auto slot = juce::Rectangle<float> (cx - 3.0f, area.getY() - 7.0f, 6.0f, area.getHeight() + 14.0f);
    g.setColour (juce::Colour (0xff050506));
    g.fillRoundedRectangle (slot, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (juce::roundToInt (slot.getBottom()), slot.getX(), slot.getRight());

    // cap
    auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, pos });
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4d5055), cap.getX(), cap.getY(), juce::Colour (0xff1e1f22),
                                             cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawHorizontalLine (juce::roundToInt (cap.getY() + 1.0f), cap.getX() + 2.0f, cap.getRight() - 2.0f);
    g.setColour (juce::Colour (0xffff6a3d));
    g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 1.0f, capW - 6.0f, 2.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    drawHardButton (g, b.getLocalBounds().toFloat().reduced (1.0f, 1.0f).withTrimmedBottom (2.0f), down, highlighted);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.isEnabled() ? colours::text : colours::faint);
    g.drawFittedText (lang::upper (b.getButtonText()), b.getLocalBounds().reduced (6, 0).withTrimmedBottom (3).translated (0, down ? 1 : 0),
                      juce::Justification::centred, 1);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int height)
{
    return Fonts::bold (juce::jmin (14.0f, (float) height * 0.5f)).withExtraKerningFactor (0.06f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    drawVfdWindow (g, juce::Rectangle<float> (0, 0, (float) w, (float) h));
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font Look::getComboBoxFont (juce::ComboBox&)       { return Fonts::mono (14.0f); }
juce::Font Look::getPopupMenuFont()                      { return Fonts::label (15.0f); }
juce::Font Look::getAlertWindowTitleFont()               { return Fonts::bold (18.0f); }
juce::Font Look::getAlertWindowMessageFont()             { return Fonts::label (15.0f); }
juce::Font Look::getAlertWindowFont()                    { return Fonts::label (14.0f); }
juce::Font Look::getLabelFont (juce::Label& l)           { return Fonts::label (juce::jmax (11.0f, l.getFont().getHeight())); }

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    drawLed (g, { r.getX() + 8.0f, r.getCentreY() }, 8.0f, b.getToggleState());
    g.setFont (Fonts::label (14.0f));
    g.setColour (b.isEnabled() ? colours::label : colours::faint);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (20), juce::Justification::centredLeft, 2);
}

void Look::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    if (e.isReadOnly() && e.findColour (juce::TextEditor::backgroundColourId) == juce::Colours::transparentBlack)
        return;
    drawVfdWindow (g, juce::Rectangle<float> (0, 0, (float) w, (float) h));
}

void Look::drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) {}

//==============================================================================
// Knob
//==============================================================================
Knob::Knob (const juce::String& l, KnobStyle s, int maxValue) : labelText (l), style (s)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange (0.0, (double) maxValue, 1.0);
    slider.setMouseDragSensitivity (s == KnobStyle::Brass ? 320 : 220);
    slider.setVelocityBasedMode (false);
    slider.setScrollWheelEnabled (true);
    slider.getProperties().set ("knobStyle", (int) s);
    slider.onValueChange = [this]
    {
        repaint();
        if (! updating && onChange)
            onChange ((int) slider.getValue());
    };
    slider.onDragEnd = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

void Knob::setValue (int v)
{
    const juce::ScopedValueSetter<bool> sv (updating, true);
    slider.setValue (v, juce::dontSendNotification);
    repaint();
}

void Knob::resized()
{
    const int k = knobSize (style);
    slider.setBounds (getLocalBounds().withTrimmedTop (topRoom (style)).removeFromTop (k).withSizeKeepingCentre (k, k));
}

void Knob::paint (juce::Graphics& g)
{
    const int k = knobSize (style);
    const bool big = style == KnobStyle::Brass;
    if (! big)
    {
        // printed scale: a tick every tenth, longer at both ends and in the middle, numbers at the ends
        const auto c = slider.getBounds().toFloat().getCentre();
        const float R = (float) k * 0.5f;
        for (int i = 0; i <= 10; ++i)
        {
            if (i == 5 && scaleMid.isNotEmpty())
                continue;   // the "0" takes the top tick's place
            const float a = knobAngle ((float) i / 10.0f);
            const bool major = i == 0 || i == 5 || i == 10;
            g.setColour (major ? colours::silk : colours::silkDim);
            g.drawLine (juce::Line<float> (polar (c, R + 2.5f, a), polar (c, R + (major ? 7.5f : 5.5f), a)), major ? 1.3f : 1.0f);
        }
        g.setFont (Fonts::bold (10.5f).withExtraKerningFactor (0.04f));
        g.setColour (colours::silk);
        for (const auto& [text, side] : { std::pair<juce::String, float> { scaleLow, -1.0f }, { scaleHigh, 1.0f } })
        {
            const auto p = polar (c, R + 11.0f, knobAngle (side > 0 ? 1.0f : 0.0f));
            const bool word = text.length() > 2;   // OSC ... NOISE: smaller, tucked under the end tick
            g.setFont (word ? Fonts::bold (8.5f).withExtraKerningFactor (0.04f) : Fonts::bold (10.5f).withExtraKerningFactor (0.04f));
            g.drawText (text, juce::Rectangle<float> (40.0f, 11.0f).withCentre (p.translated (side * (word ? -5.0f : 5.0f), word ? 4.0f : 2.0f)),
                        juce::Justification::centred);
        }
        g.setFont (Fonts::bold (10.5f).withExtraKerningFactor (0.04f));
        if (scaleMid.isNotEmpty())   // in place of the top tick
        {
            const auto top = polar (c, R + 5.0f, knobAngle (0.5f));
            g.drawText (scaleMid, juce::Rectangle<float> (20.0f, 10.0f).withCentre (top.translated (0.0f, -0.5f)), juce::Justification::centred);
        }
    }

    auto area = getLocalBounds().withTrimmedTop (topRoom (style) + k + (big ? 4 : 10));
    const auto font = big ? Fonts::bold (13.5f).withExtraKerningFactor (0.08f) : Fonts::label (12.0f).withExtraKerningFactor (0.04f);
    const bool twoLines = ! big && labelStyle == LabelStyle::Plain && area.getHeight() >= 26 + 14 + 10;
    const int labelH = big ? 17 : (twoLines ? 26 : 16);
    drawControlLabel (g, area.removeFromTop (labelH), lang::upper (labelText), font, labelStyle,
                      big ? juce::Colour (0xffdcdee1) : colours::label, twoLines ? 2 : 1);
    // the value in a small segment readout, brighter while the knob is under the mouse or turned
    const int v = (int) slider.getValue();
    drawSegmentReadout (g, readoutBounds(), valueText ? valueText (v) : juce::String (v), 3, slider.isMouseOverOrDragging());
}

juce::Rectangle<float> Knob::readoutBounds() const
{
    const bool big = style == KnobStyle::Brass;
    const int top = topRoom (style) + knobSize (style) + (big ? 4 : 10);
    const bool twoLines = ! big && labelStyle == LabelStyle::Plain && getHeight() - top >= 26 + 14 + 10;
    const float y = (float) (top + (big ? 17 : (twoLines ? 26 : 16)) + (big ? 2 : 1));
    const float rh = big ? 24.0f : 18.0f, rw = big ? 56.0f : 40.0f;
    return juce::Rectangle<float> (rw, rh).withCentre ({ (float) getWidth() * 0.5f, y + rh * 0.5f });
}

//==============================================================================
// Fader
//==============================================================================
Fader::Fader (const juce::String& l) : labelText (l)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange (0.0, 254.0, 1.0);
    slider.setVelocityBasedMode (false);
    slider.onValueChange = [this]
    {
        repaint();
        if (! updating && onChange)
            onChange ((int) slider.getValue());
    };
    addAndMakeVisible (slider);
}

void Fader::setValue (int v)
{
    const juce::ScopedValueSetter<bool> sv (updating, true);
    slider.setValue (v, juce::dontSendNotification);
    repaint();
}

void Fader::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (34));
}

void Fader::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().removeFromBottom (32);
    g.setFont (Fonts::bold (11.5f).withExtraKerningFactor (0.06f));
    g.setColour (colours::label);
    g.drawText (lang::upper (labelText), area.removeFromTop (16), juce::Justification::centred);
    drawSegmentReadout (g, readoutBounds(), juce::String ((int) slider.getValue()), 3, slider.isMouseOverOrDragging());
}

juce::Rectangle<float> Fader::readoutBounds() const
{
    const auto box = getLocalBounds().removeFromBottom (16).toFloat();
    return juce::Rectangle<float> (juce::jmin (32.0f, box.getWidth()), 15.0f).withCentre (box.getCentre());
}

//==============================================================================
// Selector
//==============================================================================
Selector::Selector (const juce::String& l, bool labelOnLeft) : labelText (l), labelLeft (labelOnLeft)
{
    window.onClick = [this] { showList(); };
    window.onWheel = [this] (int d) { step (d); };
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    window.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (window);
    addAndMakeVisible (prev);
    addAndMakeVisible (next);
}

void Selector::setItems (std::vector<Item> i)
{
    items = std::move (i);
    repaint();
}

void Selector::setValue (int v)
{
    if (v != value)
    {
        value = v;
        repaint();
    }
}

void Selector::step (int delta)
{
    if (items.empty())
        return;
    int index = -1;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].value == value)
            index = (int) i;
    const int n = (int) items.size();
    const int newIndex = index < 0 ? 0 : juce::jlimit (0, n - 1, index + delta);
    if (newIndex == index)
        return;
    value = items[(size_t) newIndex].value;
    repaint();
    if (onChange)
        onChange (value);
}

void Selector::showList()
{
    juce::PopupMenu m;
    if (labelText.isNotEmpty())
        m.addSectionHeader (labelText);
    for (const auto& it : items)
        m.addItem (it.value + 1000, it.text, true, it.value == value);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (window).withMinimumWidth (window.getWidth()),
                     [safe = juce::Component::SafePointer<Selector> (this)] (int r)
                     {
                         if (safe == nullptr || r < 1000)
                             return;
                         safe->value = r - 1000;
                         safe->repaint();
                         if (safe->onChange)
                             safe->onChange (safe->value);
                     });
}

void Selector::resized()
{
    auto b = getLocalBounds();
    juce::Rectangle<int> win;
    if (labelLeft)
    {
        const int lw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (Fonts::label (12.0f), lang::upper (labelText)) * 1.1f) + 16;
        b.removeFromLeft (lw);
        win = b.withSizeKeepingCentre (b.getWidth(), 28);
    }
    else
    {
        win = b.removeFromTop (28);
    }
    prev.setBounds (win.removeFromLeft (22));
    next.setBounds (win.removeFromRight (22));
    window.setBounds (win);
}

void Selector::paint (juce::Graphics& g)
{
    auto whole = prev.getBounds().getUnion (next.getBounds()).getUnion (window.getBounds()).toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (whole.expanded (1.0f), 4.0f);
    auto w = window.getBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d0d06), w.getX(), w.getY(), juce::Colour (0xff120703), w.getX(), w.getBottom(), false));
    g.fillRect (w);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), w.getX(), w.getY(), juce::Colours::transparentBlack,
                                             w.getX(), w.getY() + 5.0f, false));
    g.fillRect (w);

    juce::String text = "Value " + juce::String (value);
    for (const auto& it : items)
        if (it.value == value)
            text = it.text;

    const auto glyphBox = window.getBounds().toFloat().withSizeKeepingCentre (40.0f, 14.0f);
    g.setColour (colours::vfd.withAlpha (0.35f));
    if (! drawGlyph (g, glyphBox, text, 3.2f))
    {
        g.setFont (Fonts::mono (14.0f));
        g.drawFittedText (text, window.getBounds().reduced (3, 0).translated (0, 0), juce::Justification::centred, 1, 0.8f);
    }
    g.setColour (colours::vfd);
    if (! drawGlyph (g, glyphBox, text, 1.8f))
        g.drawFittedText (text, window.getBounds().reduced (3, 0), juce::Justification::centred, 1, 0.8f);

    g.setFont (Fonts::label (12.0f).withExtraKerningFactor (0.05f));
    g.setColour (colours::label);
    if (labelLeft)
        g.drawText (lang::upper (labelText), getLocalBounds().withRight (prev.getX() - 8), juce::Justification::centredRight);
    else
        g.drawFittedText (lang::upper (labelText), getLocalBounds().withTop (window.getBottom() + 4), juce::Justification::centredTop, 1, 0.85f);
}

void Selector::Arrow::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    auto top = down ? juce::Colour (0xff2a2c30) : juce::Colour (0xff46494e);
    if (highlighted && ! down) top = top.brighter (0.1f);
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), juce::Colour (0xff25272a), r.getX(), r.getBottom(), false));
    juce::Path bg;
    bg.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4.0f, 4.0f, isLeft, ! isLeft, isLeft, ! isLeft);
    g.fillPath (bg);
    const auto c = r.getCentre();
    juce::Path tri;
    if (isLeft) tri.addTriangle (c.x + 3.0f, c.y - 4.0f, c.x + 3.0f, c.y + 4.0f, c.x - 3.0f, c.y);
    else        tri.addTriangle (c.x - 3.0f, c.y - 4.0f, c.x - 3.0f, c.y + 4.0f, c.x + 3.0f, c.y);
    g.setColour (isEnabled() ? juce::Colour (0xffb9bbbf) : colours::faint);
    g.fillPath (tri);
}

//==============================================================================
// VfdNumber
//==============================================================================
VfdNumber::VfdNumber (const juce::String& l, int minValue, int maxValue) : labelText (l)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange ((double) minValue, (double) maxValue, 1.0);
    slider.setMouseDragSensitivity (600);
    slider.setVelocityBasedMode (false);
    slider.setScrollWheelEnabled (true);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    slider.getProperties().set ("vfdNumber", true);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void VfdNumber::resized()
{
    slider.setBounds (getLocalBounds().removeFromTop (windowHeight));
}

void VfdNumber::paint (juce::Graphics& g)
{
    auto win = getLocalBounds().removeFromTop (windowHeight).toFloat();
    drawVfdWindow (g, win);
    const int v = (int) slider.getValue();
    const auto text = valueText ? valueText (v) : juce::String (v);
    auto textArea = win.reduced (12.0f, 2.0f).withTrimmedRight (10.0f).toNearestInt();
    g.setFont (Fonts::mono (30.0f));
    g.setColour (colours::vfd.withAlpha (0.3f));
    g.drawFittedText (text, textArea.translated (0, 1), juce::Justification::centred, 1, 0.7f);
    g.setColour (colours::vfd);
    g.drawFittedText (text, textArea, juce::Justification::centred, 1, 0.7f);

    // up / down marks: it is dragged, not turned
    const float x = win.getRight() - 12.0f, cy = win.getCentreY();
    juce::Path marks;
    marks.addTriangle (x - 4.0f, cy - 4.0f, x + 4.0f, cy - 4.0f, x, cy - 10.0f);
    marks.addTriangle (x - 4.0f, cy + 4.0f, x + 4.0f, cy + 4.0f, x, cy + 10.0f);
    g.setColour (colours::vfdDim.withAlpha (0.7f));
    g.fillPath (marks);

    g.setFont (Fonts::label (12.0f).withExtraKerningFactor (0.05f));
    g.setColour (colours::label);
    g.drawFittedText (lang::upper (labelText), getLocalBounds().withTrimmedTop (windowHeight + 4), juce::Justification::centredTop, 1, 0.8f);
}

//==============================================================================
// RotarySwitch
//==============================================================================
RotarySwitch::RotarySwitch (const juce::String& l) : labelText (l)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RotarySwitch::setItems (std::vector<Item> i, float spanDegrees)
{
    items = std::move (i);
    const int n = (int) items.size();
    span = spanDegrees > 0.0f ? spanDegrees
                              : (n <= 2 ? 60.0f : n == 3 ? 90.0f : n == 4 ? 130.0f : n == 5 ? 180.0f : n == 6 ? 240.0f : n <= 8 ? 300.0f : 270.0f);
    repaint();
}

void RotarySwitch::setValue (int v)
{
    if (v != value)
    {
        value = v;
        repaint();
    }
}

int RotarySwitch::indexOf (int v) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].value == v)
            return (int) i;
    return -1;
}

float RotarySwitch::angleOf (int index) const
{
    const int n = (int) items.size();
    if (n < 2)
        return 0.0f;
    return juce::degreesToRadians (-span * 0.5f + span * (float) index / (float) (n - 1));
}

juce::String RotarySwitch::shownLegend (int index) const
{
    const auto& it = items[(size_t) index];
    if (it.legend.isNotEmpty())
        return it.legend;
    return it.value == value ? it.name : juce::String();
}

juce::Rectangle<float> RotarySwitch::legendBox (int index) const
{
    const auto text = shownLegend (index);
    const auto font = Fonts::bold (legendSize).withExtraKerningFactor (0.04f);
    const float glyph = glyphWidth (text, legendSize);
    const auto lines = juce::StringArray::fromLines (text);   // "FILTER\nENV": two lines, as printed
    float tw = glyph + 2.0f;
    if (glyph <= 0.0f)
        for (const auto& l : lines)
            tw = juce::jmax (tw, juce::GlyphArrangement::getStringWidth (font, l) + 1.0f);
    const float th = legendSize * (float) juce::jmax (1, lines.size());
    const float a = angleOf (index);
    const float dist = radius + 9.0f + std::abs (std::sin (a)) * tw * 0.5f + std::abs (std::cos (a)) * th * 0.5f;
    const auto box = juce::Rectangle<float> (tw, th).withCentre (polar (centre(), dist, a));
    return box.withY (juce::jmax (1.0f, box.getY()));   // a two-line legend at the top stays inside
}

void RotarySwitch::paint (juce::Graphics& g)
{
    const auto c = centre();
    const int current = indexOf (value);
    const auto font = Fonts::bold (legendSize).withExtraKerningFactor (0.04f);
    g.setFont (font);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const float a = angleOf (i);
        const bool lit = i == current;
        g.setColour (lit ? colours::vfd : colours::silkDim);
        g.drawLine (juce::Line<float> (polar (c, radius + 2.5f, a), polar (c, radius + 6.5f, a)), lit ? 2.0f : 1.2f);
        const auto text = shownLegend (i);
        if (text.isEmpty())
            continue;
        const auto box = legendBox (i);
        if (lit)
        {
            g.setColour (colours::vfd.withAlpha (0.3f));
            if (! drawGlyph (g, box.translated (0.0f, 0.5f), text, 2.4f))
                g.drawFittedText (text, box.expanded (2.0f).translated (0.0f, 0.5f).toNearestInt(), juce::Justification::centred, 2, 1.0f);
        }
        g.setColour (lit ? colours::vfd : colours::silk);
        if (! drawGlyph (g, box, text))
            g.drawFittedText (text, box.expanded (2.0f).toNearestInt(), juce::Justification::centred, 2, 1.0f);
    }

    drawSwitchKnob (g, c, radius, current >= 0 ? angleOf (current) : 0.0f);

    g.setFont (Fonts::label (12.0f).withExtraKerningFactor (0.05f));
    g.setColour (colours::label);
    g.drawFittedText (lang::upper (labelText), getLocalBounds().removeFromBottom (15), juce::Justification::centred, 1, 0.8f);
}

void RotarySwitch::choose (int index)
{
    if (index < 0 || index >= (int) items.size() || items[(size_t) index].value == value)
        return;
    value = items[(size_t) index].value;
    repaint();
    if (onChange)
        onChange (value);
}

void RotarySwitch::mouseDown (const juce::MouseEvent& e)
{
    dragged = pickedLegend = false;
    dragFrom = indexOf (value);
    for (int i = 0; i < (int) items.size(); ++i)
        if (shownLegend (i).isNotEmpty() && legendBox (i).expanded (3.0f, 2.0f).contains (e.position))
        {
            pickedLegend = true;
            choose (i);
            return;
        }
}

void RotarySwitch::mouseDrag (const juce::MouseEvent& e)
{
    if (pickedLegend || items.empty())
        return;
    const int moved = e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY();
    if (std::abs (moved) > 3)
        dragged = true;
    if (dragged)
        choose (juce::jlimit (0, (int) items.size() - 1, juce::jmax (0, dragFrom) + moved / 14));
}

void RotarySwitch::mouseUp (const juce::MouseEvent& e)
{
    if (! pickedLegend && ! dragged && e.mouseWasClicked())
        showList();
}

void RotarySwitch::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& d)
{
    if (std::abs (d.deltaY) > 0.0f && ! items.empty())
        choose (juce::jlimit (0, (int) items.size() - 1, juce::jmax (0, indexOf (value)) + (d.deltaY > 0 ? -1 : 1)));
}

void RotarySwitch::showList()
{
    juce::PopupMenu m;
    if (labelText.isNotEmpty())
        m.addSectionHeader (labelText);
    for (const auto& it : items)
        m.addItem (it.value + 1000, it.name, true, it.value == value);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<RotarySwitch> (this)] (int r)
                     {
                         if (safe != nullptr && r >= 1000)
                             safe->choose (safe->indexOf (r - 1000));
                     });
}

//==============================================================================
// LedSelector
//==============================================================================
LedSelector::LedSelector (const juce::String& l) : labelText (l)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LedSelector::setItems (std::vector<RotarySwitch::Item> i)
{
    items = std::move (i);
    repaint();
}

void LedSelector::setValue (int v)
{
    if (v != value)
    {
        value = v;
        repaint();
    }
}

float LedSelector::blockLeft() const
{
    const auto font = Fonts::bold (11.0f).withExtraKerningFactor (0.04f);
    float widest = 0.0f;
    for (const auto& it : items)
        widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, it.legend));
    const float blockW = (float) buttonWidth + 18.0f + widest;   // button, LED, legend
    return juce::jmax (0.0f, ((float) getWidth() - blockW) * 0.5f);
}

juce::Rectangle<float> LedSelector::controlBounds() const
{
    return { blockLeft() - 1.0f, 0.0f, (float) buttonWidth + 17.0f, (float) blockHeight() };
}

juce::Rectangle<float> LedSelector::buttonArea() const
{
    return { blockLeft(), ((float) blockHeight() - (float) buttonHeight) * 0.5f, (float) buttonWidth, (float) buttonHeight };
}

juce::Rectangle<float> LedSelector::rowArea (int index) const
{
    const float top = ((float) blockHeight() - (float) items.size() * (float) rowHeight) * 0.5f;
    const float x = blockLeft() + (float) buttonWidth + 6.0f;
    return { x, top + (float) index * (float) rowHeight, (float) getWidth() - x, (float) rowHeight };
}

void LedSelector::paint (juce::Graphics& g)
{
    drawHardButton (g, buttonArea(), pressed, isMouseOver() && ! pressed);
    const auto font = Fonts::bold (11.0f).withExtraKerningFactor (0.04f);
    g.setFont (font);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto row = rowArea (i);
        const bool lit = items[(size_t) i].value == value;
        drawLed (g, { row.getX() + 4.0f, row.getCentreY() }, 6.0f, lit);
        g.setColour (lit ? juce::Colours::white : colours::silk);
        g.drawText (items[(size_t) i].legend, row.withTrimmedLeft (12.0f), juce::Justification::centredLeft, false);
    }
    g.setFont (Fonts::label (12.0f).withExtraKerningFactor (0.05f));
    g.setColour (colours::label);
    g.drawFittedText (lang::upper (labelText), getLocalBounds().withTop (blockHeight() + 6), juce::Justification::centredTop, 1, 0.8f);
}

void LedSelector::choose (int index)
{
    if (index < 0 || index >= (int) items.size() || items[(size_t) index].value == value)
        return;
    value = items[(size_t) index].value;
    repaint();
    if (onChange)
        onChange (value);
}

void LedSelector::mouseDown (const juce::MouseEvent& e)
{
    if (buttonArea().expanded (3.0f).contains (e.position))
    {
        pressed = true;
        repaint();
        return;
    }
    for (int i = 0; i < (int) items.size(); ++i)
        if (rowArea (i).contains (e.position))
            choose (i);
}

void LedSelector::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;
    pressed = false;
    repaint();
    if (buttonArea().expanded (3.0f).contains (e.position) && ! items.empty())
    {
        int index = 0;
        for (int i = 0; i < (int) items.size(); ++i)
            if (items[(size_t) i].value == value)
                index = (i + 1) % (int) items.size();
        choose (index);
    }
}

//==============================================================================
// Buttons
//==============================================================================
LedButton::LedButton (const juce::String& buttonText, LedPlace p) : juce::Button (buttonText), place (p) {}

void LedButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    juce::Rectangle<float> body = r;
    if (place == LedPlace::Below)
        body = r.removeFromTop (r.getHeight() - 11.0f);
    body = body.reduced (1.0f, 0.0f).withTrimmedBottom (3.0f);
    drawHardButton (g, body, down, highlighted);

    auto textArea = body.reduced (8.0f, 0.0f).translated (0.0f, down ? 1.0f : 0.0f);
    if (place == LedPlace::Right)
    {
        drawLed (g, { body.getRight() - 12.0f, body.getCentreY() + (down ? 1.0f : 0.0f) }, 7.0f, lit);
        textArea.removeFromRight (14.0f);
    }
    else if (place == LedPlace::Below)
    {
        drawLed (g, { r.getCentreX(), r.getBottom() - 4.0f }, 6.0f, lit);
    }

    const float fh = juce::jlimit (10.5f, 14.0f, body.getHeight() * 0.48f);
    g.setFont (Fonts::bold (fh).withExtraKerningFactor (0.08f));
    g.setColour (! isEnabled() ? colours::faint : (textColour ? *textColour : (lit ? juce::Colours::white : colours::label)));
    g.drawFittedText (lang::upper (getButtonText()), textArea.toNearestInt(),
                      place == LedPlace::Right ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.75f);
}

void CopperButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    drawCopper (g, r, 4.0f, isEnabled(), down);
    if (highlighted && ! down && isEnabled())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (r, 4.0f);
    }
    g.setFont (Fonts::bold (fontHeight).withExtraKerningFactor (0.14f));
    g.setColour (isEnabled() ? juce::Colour (0xff1e0b03) : colours::faint);
    g.drawFittedText (lang::upper (getButtonText()), r.toNearestInt().translated (0, down ? 1 : 0), juce::Justification::centred, 1, 0.8f);
}

void Pad::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    auto square = r.removeFromTop (r.getWidth());
    g.setColour (juce::Colour (0xff0b0b0c));
    g.fillRoundedRectangle (square, 7.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (square.reduced (0.5f), 7.0f, 1.0f);
    auto inner = square.reduced (4.0f).translated (0.0f, down ? 1.0f : 0.0f);
    drawCopper (g, inner, 5.0f, lit, down);
    if (highlighted && ! lit)
    {
        g.setColour (colours::copper.withAlpha (0.10f));
        g.fillRoundedRectangle (inner, 5.0f);
    }
    g.setFont (Fonts::bold (12.0f).withExtraKerningFactor (0.08f));
    g.setColour (lit ? juce::Colour (0xffffb48a) : colours::dim);
    g.drawText (lang::upper (getButtonText()), r.withTrimmedTop (6.0f), juce::Justification::centredTop);
}

} // namespace atelier
