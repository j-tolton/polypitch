#pragma once

#include <JuceHeader.h>

namespace polypitch::style
{
    // JUCE's default sans is much wider, and doesn't match the web app's system font.
    inline juce::Font font(float size, bool bold = false)
    {
        return juce::FontOptions("Segoe UI", size, bold ? juce::Font::bold : juce::Font::plain);
    }
}
