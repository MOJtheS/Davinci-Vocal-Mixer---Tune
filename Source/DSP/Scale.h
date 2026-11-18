#pragma once
#include <cmath>
#include <cstdint>

namespace humrig
{
inline constexpr int kNumScales = 10;

inline const char* const kKeyNames[12]   = { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
inline const char* const kScaleNames[kNumScales] = { "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
                                                     "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian" };

// Bit n set = semitone n above the key's root belongs to the scale.
inline uint16_t scaleMask (int scale) noexcept
{
    static constexpr int degrees[kNumScales][8] = {
        { 0, 1, 2, 3, 4, 5, 6, 7 },        // chromatic (handled below)
        { 0, 2, 4, 5, 7, 9, 11, -1 },      // major
        { 0, 2, 3, 5, 7, 8, 10, -1 },      // natural minor
        { 0, 2, 3, 5, 7, 8, 11, -1 },      // harmonic minor
        { 0, 2, 3, 5, 7, 9, 11, -1 },      // melodic minor (ascending)
        { 0, 2, 3, 5, 7, 9, 10, -1 },      // dorian
        { 0, 1, 3, 5, 7, 8, 10, -1 },      // phrygian
        { 0, 2, 4, 6, 7, 9, 11, -1 },      // lydian
        { 0, 2, 4, 5, 7, 9, 10, -1 },      // mixolydian
        { 0, 1, 3, 5, 6, 8, 10, -1 } };    // locrian
    if (scale <= 0 || scale >= kNumScales) return 0x0FFF;
    uint16_t m = 0;
    for (int d : degrees[scale]) if (d >= 0) m |= (uint16_t) (1u << d);
    return m;
}

inline bool inScale (int midiNote, int key, uint16_t mask) noexcept
{
    int pc = (midiNote - key) % 12;
    if (pc < 0) pc += 12;
    return (mask >> pc) & 1;
}

// Nearest in-scale MIDI note (integer) to a continuous MIDI pitch.
inline int nearestInScale (float midi, int key, uint16_t mask) noexcept
{
    const int base = (int) std::floor (midi);
    int best = base;
    float bestDist = 1.0e9f;
    for (int n = base - 2; n <= base + 3; ++n)
    {
        if (! inScale (n, key, mask)) continue;
        const float d = std::fabs (midi - (float) n);
        if (d < bestDist) { bestDist = d; best = n; }
    }
    return best;
}
} // namespace humrig
