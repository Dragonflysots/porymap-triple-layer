#ifndef BEHAVIORCOLOR_H
#define BEHAVIORCOLOR_H

// CUSTOM ENGINE: deterministic color-per-behavior-ID generator, shared by the Tileset Editor's
// Prefab Behaviour tab and the main window's Behaviors tab. Nothing is hardcoded per ID: hues are
// stepped around the color wheel by the golden angle, so neighbouring IDs land far apart (easy to
// tell apart on the map) and a given ID's color never depends on how many behaviors exist --
// custom behaviors added later can't shift the colors of existing ones.

#include <QColor>
#include <cmath>

namespace BehaviorColor {

inline QColor forId(int id) {
    // 0x00 (MB_NORMAL, "nothing special") is a real entry like every other: a calm slate blue-grey, so a field with no special behavior is
    // still SEEN (with its "00"), like an elevation of 0 is -- and can never be mistaken for a colour that means something.
    if (id == 0)
        return QColor(104, 122, 145);
    int hue = static_cast<int>(id * 137.50776405) % 360;
    int sat = 170 + (id % 3) * 28;   // 170-226
    int val = 205 + (id % 2) * 35;   // 205-240
    return QColor::fromHsv(hue, sat, val);
}

// Black or white, whichever reads better on `background` (the one with the higher WCAG contrast
// ratio). QColor::lightness() is no good for this: saturated colors such as pure green or blue have
// the same "lightness" but look very different.
inline QColor inkFor(const QColor &background) {
    auto linear = [](int channel) {
        double c = channel / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    double luminance = 0.2126 * linear(background.red()) + 0.7152 * linear(background.green()) + 0.0722 * linear(background.blue());
    double contrastWithWhite = 1.05 / (luminance + 0.05);
    double contrastWithBlack = (luminance + 0.05) / 0.05;
    return contrastWithBlack >= contrastWithWhite ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

} // namespace BehaviorColor

#endif // BEHAVIORCOLOR_H
