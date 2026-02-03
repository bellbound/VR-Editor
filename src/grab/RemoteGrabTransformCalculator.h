#pragma once

#include <RE/Skyrim.h>

namespace Grab {

// Forward declaration
struct RemoteGrabObject;

// Result of a transform calculation for a single grabbed object
struct ComputedObjectTransform {
    RE::NiTransform transform;     // Final visual transform (position + rotation matrix)
    RE::NiPoint3 eulerAngles;      // Lossless Euler angles for game data
    bool groundSnapped = false;    // True if object was snapped to ground
};

// Centralizes transform computation for remote-grabbed objects.
// This logic is used by:
//   - UpdateAllObjects (per-frame visual updates)
//   - RecordActions (action history recording)
//   - FinalizePositions (final position sync on release)
//
// By extracting this to a single class, we ensure all three use cases
// compute identical transforms, preventing drift or inconsistency.
class RemoteGrabTransformCalculator
{
public:
    // Calculate the final transform for a grabbed object
    // Parameters:
    //   obj: The grabbed object with its initial transform and offset
    //   centerPos: Current (smoothed) center position of the group
    //   smoothedAngle: Current Z-rotation angle (extracted from smoothed rotation)
    //   groundSnapEnabled: Whether to raycast and snap to ground
    //   rotationGridEnabled: Whether to snap final rotation to world grid
    //   rotationGridDegrees: Grid size in degrees (e.g., 15.0f for 15° increments)
    // Returns: Computed transform with both matrix and Euler representation
    static ComputedObjectTransform Calculate(
        const RemoteGrabObject& obj,
        const RE::NiPoint3& centerPos,
        float smoothedAngle,
        bool groundSnapEnabled,
        bool rotationGridEnabled = false,
        float rotationGridDegrees = 15.0f
    );

    // Calculate just the floating position (without ground snap or rotation matrix)
    // Useful for raycast origin in ground snap calculations
    static RE::NiPoint3 CalculateFloatingPosition(
        const RemoteGrabObject& obj,
        const RE::NiPoint3& centerPos,
        float smoothedAngle
    );

    // Calculate rotated offset (orbital motion around center)
    static RE::NiPoint3 CalculateRotatedOffset(
        const RE::NiPoint3& offset,
        float angle
    );

private:
    // Internal: Calculate ground-snapped transform
    static ComputedObjectTransform CalculateWithGroundSnap(
        const RemoteGrabObject& obj,
        const RE::NiPoint3& floatingPos,
        float smoothedAngle,
        bool rotationGridEnabled,
        float rotationGridDegrees
    );

    // Internal: Calculate floating (non-snapped) transform
    static ComputedObjectTransform CalculateFloating(
        const RemoteGrabObject& obj,
        const RE::NiPoint3& floatingPos,
        float smoothedAngle,
        bool rotationGridEnabled,
        float rotationGridDegrees
    );

    // Helper: Snap an angle (radians) to a grid (degrees)
    static float SnapAngleToWorldGrid(float angleRad, float gridDegrees);
};

} // namespace Grab
