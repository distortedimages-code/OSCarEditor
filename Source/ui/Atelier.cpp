#include "Atelier.h"

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
        ticks (g, c, R - 0.5f, R * 0.09f, R * 0.14f, juce::Colour (0xff8a8d92), 1.2f);
        auto cap = juce::Rectangle<float> (R * 1.38f, R * 1.38f).withCentre (c);
        dropShadow (g, cap, R * 0.18f, 0.75f);
        fillConic (g, cap, { { 0.0f, juce::Colour (0xfff4f4f4) }, { 0.17f, juce::Colour (0xff9a9ca0) },
                             { 0.33f, juce::Colour (0xffe6e7e9) }, { 0.5f, juce::Colour (0xff7d8085) },
                             { 0.67f, juce::Colour (0xfff0f0f0) }, { 0.83f, juce::Colour (0xff8d9094) },
                             { 1.0f, juce::Colour (0xfff4f4f4) } }, 0.35f);
        g.setColour (juce::Colour (0xff111111));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);
        auto face = juce::Rectangle<float> (R * 0.92f, R * 0.92f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, face.getX() + face.getWidth() * 0.4f, face.getY() + face.getHeight() * 0.35f,
                                                 juce::Colour (0xffa3a6aa), face.getRight(), face.getBottom(), true));
        g.fillEllipse (face);
        g.setColour (juce::Colour (0xff1a1a1a));
        g.drawLine (juce::Line<float> (polar (c, R * 0.30f, angle), polar (c, R * 0.64f, angle)), juce::jmax (1.6f, R * 0.08f));
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
    const auto style = (KnobStyle) (int) s.getProperties().getWithDefault ("knobStyle", 0);
    drawKnob (g, juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h), pos, style);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                             juce::Slider::SliderStyle style, juce::Slider& s)
{
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
    slider.setBounds (getLocalBounds().removeFromTop (k).withSizeKeepingCentre (k, k));
}

void Knob::paint (juce::Graphics& g)
{
    const int k = knobSize (style);
    auto area = getLocalBounds().withTrimmedTop (k + 4);
    const bool big = style == KnobStyle::Brass;
    g.setFont (big ? Fonts::bold (13.0f).withExtraKerningFactor (0.08f) : Fonts::label (11.5f).withExtraKerningFactor (0.04f));
    g.setColour (big ? juce::Colour (0xffdcdee1) : colours::label);
    const int labelH = big ? 16 : 26;
    g.drawFittedText (lang::upper (labelText), area.removeFromTop (labelH), juce::Justification::centredTop, 2, 0.9f);
    area.removeFromTop (big ? 2 : 1);
    g.setFont (Fonts::mono (big ? 15.0f : 12.5f));
    g.setColour (big ? colours::vfd : colours::valueText);
    const int v = (int) slider.getValue();
    g.drawText (valueText ? valueText (v) : juce::String (v), area.removeFromTop (14), juce::Justification::centredTop);
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
    g.setFont (Fonts::mono (12.5f));
    g.setColour (colours::valueText);
    g.drawText (juce::String ((int) slider.getValue()), area.removeFromTop (14), juce::Justification::centred);
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
        const int lw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (Fonts::label (11.5f), lang::upper (labelText)) * 1.1f) + 16;
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

    g.setFont (Fonts::mono (14.0f));
    g.setColour (colours::vfd.withAlpha (0.35f));
    g.drawFittedText (text, window.getBounds().reduced (3, 0).translated (0, 0), juce::Justification::centred, 1, 0.8f);
    g.setColour (colours::vfd);
    g.drawFittedText (text, window.getBounds().reduced (3, 0), juce::Justification::centred, 1, 0.8f);

    g.setFont (Fonts::label (11.5f).withExtraKerningFactor (0.05f));
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
