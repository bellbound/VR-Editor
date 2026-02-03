#pragma once

#include "TransformSmoother.h"
#include <RE/Skyrim.h>

namespace Grab {

// Grid override configuration - aligns grid to specific reference points
// When active, the grid is offset and scaled so that:
// - One grid point aligns exactly with position A
// - Another grid point aligns exactly with position B
// - Rotation steps are offset to align with position A's Z rotation
struct GridOverride {
    RE::NiPoint3 offset{0, 0, 0};   // Grid origin offset
    float scale = 1.0f;              // Grid scale multiplier (applied to default grid size)
    float rotationOffset = 0.0f;     // Rotation offset in radians
    bool active = false;
};

// Handles snap-to-grid position calculations with motion smoothing
// to prevent jarring transitions when objects snap to new grid cells.
//
// Design Notes:
// - Position snapping uses a world-aligned grid (snaps to multiples of gridSize)
// - Rotation snapping happens per-object in RemoteGrabTransformCalculator
//   to align each object's final world rotation to cardinal angles
// - Motion smoothing interpolates between grid cells to avoid jarring jumps
// - Grid Override: Can align grid to two reference positions (selection-based alignment)
class SnapToGridController
{
public:
    // Configuration defaults
    static constexpr float kDefaultPositionGridSize = 20.0f;      // Position snaps to 20-unit grid
    static constexpr int kDefaultRotationSnapStops = 24;          // Rotation snaps to 24 stops (15°)
    static constexpr float kDefaultRotationGridDegrees = 360.0f / kDefaultRotationSnapStops;
    static constexpr float kDefaultSmoothingSpeed = 16.0f;        // Smoothing interpolation speed

    // Enable/disable snap mode
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    bool IsRotationSnappingEnabled() const;

    // Configuration
    void SetPositionGridSize(float size);
    void SetRotationGridDegrees(float degrees);
    float GetPositionGridSize() const;
    float GetRotationGridDegrees() const;

    // Get effective values (accounting for grid override)
    float GetEffectivePositionGridSize() const;
    float GetEffectiveRotationOffset() const;

    // Grid Override - align grid to two reference positions
    // When override is active, the grid is offset and scaled to match selection positions
    bool HasGridOverride() const;
    const GridOverride& GetGridOverride() const;
    void SetGridOverride(const GridOverride& override);
    void ClearGridOverride();

    // Compute a grid override that aligns grid points to two positions
    // The grid scale is adjusted to be as close to default as possible
    // while ensuring both positions lie on grid points
    // Parameters:
    //   posA: First reference position (grid will pass through this point)
    //   posB: Second reference position (grid will also pass through this point)
    //   rotationA: Z rotation of first object (rotation steps will align to this)
    // Returns: GridOverride that aligns the grid to both positions
    static GridOverride ComputeGridOverride(
        const RE::NiPoint3& posA,
        const RE::NiPoint3& posB,
        float rotationA
    );

    // Reset state (call when starting a new grab)
    void Reset();

    // Result of computing a smoothed snap
    struct SnapResult {
        RE::NiPoint3 position;  // Smoothed snapped position
        float zAngle;           // Smoothed Z angle (raw, not grid-snapped here)
    };

    // Main API: Compute smoothed snapped position and angle
    // When snap is enabled, positions are snapped to grid then smoothed
    // When snap is disabled, returns raw values unchanged
    // Parameters:
    //   rawPosition: The unsnapped center position
    //   rawAngle: The unsnapped Z rotation angle (radians)
    //   deltaTime: Frame delta time for smoothing
    // Returns: Smoothed display values (snapped if enabled)
    SnapResult ComputeSmoothedSnap(
        const RE::NiPoint3& rawPosition,
        float rawAngle,
        float deltaTime
    );

    // Static helpers (can be called without instance)
    // These are useful for one-off snapping calculations
    static RE::NiPoint3 SnapPositionToGrid(const RE::NiPoint3& pos, float gridSize);
    static float SnapAngleToGrid(float angleRad, float gridDegrees);

private:
    bool m_enabled = false;
    float m_positionGridSize = kDefaultPositionGridSize;
    float m_rotationGridDegrees = kDefaultRotationGridDegrees;

    // Grid override for selection-based alignment
    GridOverride m_gridOverride;

    TransformSmoother m_smoother;
    bool m_smootherInitialized = false;
};

} // namespace Grab
