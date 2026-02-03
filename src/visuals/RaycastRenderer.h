#pragma once

#include "RE/Skyrim.h"

namespace RaycastRenderer {

// Line parameters - describes a line from start to end point
struct LineParams {
    RE::NiPoint3 start;  // Origin point (e.g., hand position)
    RE::NiPoint3 end;    // Destination point (e.g., hit location)

    // Computed properties
    RE::NiPoint3 Direction() const {
        RE::NiPoint3 diff = end - start;
        float len = Length();
        if (len <= 0.0f) {
            return {0.0f, 0.0f, 0.0f};
        }
        return diff / len;
    }

    float Length() const {
        RE::NiPoint3 diff = end - start;
        return std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
    }
};

// Visual style for the beam
enum class BeamType : uint8_t {
    Default,   // Normal selection beam
    Invalid,   // Red/off-limits style (e.g., blocked, invalid target)
};

// Show the beam with given line parameters
// type: Visual style of the beam
void Show(const LineParams& line, BeamType type = BeamType::Default);

// Hide the beam
void Hide();

// Update the beam position/orientation (call each frame while visible)
// type: Visual style of the beam
void Update(const LineParams& line, BeamType type = BeamType::Default);

// Check if the beam is currently visible
bool IsVisible();

} // namespace RaycastRenderer
