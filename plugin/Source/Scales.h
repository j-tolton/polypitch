#pragma once

#include <array>
#include <initializer_list>

// The same practice targets as the web app's picker.
namespace polypitch::scales
{
    struct Scale
    {
        const char* id;
        const char* label;
        bool isArpeggio;
        std::initializer_list<int> steps;
        int relativeMajor; // Decides whether a key is spelled with flats.
    };

    inline const std::array<Scale, 16>& all()
    {
        static const std::array<Scale, 16> list { {
            { "off",        "All notes",        false, {},                     0 },
            { "major",      "Major",            false, { 0, 2, 4, 5, 7, 9, 11 }, 0 },
            { "minor",      "Natural minor",    false, { 0, 2, 3, 5, 7, 8, 10 }, 3 },
            { "harmonic",   "Harmonic minor",   false, { 0, 2, 3, 5, 7, 8, 11 }, 3 },
            { "melodic",    "Melodic minor",    false, { 0, 2, 3, 5, 7, 9, 11 }, 3 },
            { "majpent",    "Major pentatonic", false, { 0, 2, 4, 7, 9 },        0 },
            { "minpent",    "Minor pentatonic", false, { 0, 3, 5, 7, 10 },       3 },
            { "blues",      "Blues",            false, { 0, 3, 5, 6, 7, 10 },    3 },
            { "dorian",     "Dorian",           false, { 0, 2, 3, 5, 7, 9, 10 }, 10 },
            { "mixolydian", "Mixolydian",       false, { 0, 2, 4, 5, 7, 9, 10 }, 5 },
            { "majtri",     "Major",            true,  { 0, 4, 7 },              0 },
            { "mintri",     "Minor",            true,  { 0, 3, 7 },              3 },
            { "dom7",       "Dominant 7th",     true,  { 0, 4, 7, 10 },          5 },
            { "maj7",       "Major 7th",        true,  { 0, 4, 7, 11 },          0 },
            { "min7",       "Minor 7th",        true,  { 0, 3, 7, 10 },          3 },
            { "dim7",       "Diminished 7th",   true,  { 0, 3, 6, 9 },           3 },
        } };
        return list;
    }

    inline int findIndex(const char* id)
    {
        for (size_t i = 0; i < all().size(); ++i)
        {
            const char* a = all()[i].id;
            const char* b = id;
            while (*a != 0 && *a == *b) { ++a; ++b; }
            if (*a == *b)
                return (int) i;
        }
        return 0;
    }

    inline bool usesFlats(int rootPc, const Scale& scale)
    {
        if (scale.steps.size() == 0)
            return false;
        const int major = (rootPc + scale.relativeMajor) % 12;
        return major == 1 || major == 3 || major == 5 || major == 8 || major == 10;
    }

    inline const char* noteName(int pitchClass, bool flats)
    {
        static const char* const sharpNames[] { "C", "C\xe2\x99\xaf", "D", "D\xe2\x99\xaf", "E", "F", "F\xe2\x99\xaf", "G", "G\xe2\x99\xaf", "A", "A\xe2\x99\xaf", "B" };
        static const char* const flatNames[]  { "C", "D\xe2\x99\xad", "D", "E\xe2\x99\xad", "E", "F", "G\xe2\x99\xad", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
        return (flats ? flatNames : sharpNames)[((pitchClass % 12) + 12) % 12];
    }

    // Bitmask for the tracker, or -1 when every note is a target.
    inline int targetMask(int rootPc, const Scale& scale)
    {
        if (scale.steps.size() == 0)
            return -1;
        int mask = 0;
        for (int step : scale.steps)
            mask |= 1 << ((rootPc + step) % 12);
        return mask;
    }
}
