#pragma once

#include "TransformSmoother.h"
#include <RE/Skyrim.h>

namespace Grab {

// HandRotationTransformer: Transforms VR hand rotation into object rotation.
//
// Usage:
// 1. Call Start() when the user begins rotation (e.g., trigger pressed)
// 2. Call Update() each frame to get the smoothed rotation delta
// 3. Call GetRotationDelta() to get the current rotation delta (matrix)
// 4. Call GetEulerDelta() to get the current rotation delta (euler angles)
// 5. Call Finish() when done - this "bakes in" the rotation so it doesn't snap back
//
// Features:
// - Inverted rotation (moving hand left rotates object right)
// - Smoothed rotation using exponential interpolation
// - Tracks cumulative rotation across the gesture
class HandRotationTransformer
{
public:
    HandRotationTransformer() = default;

    // Start tracking rotation from the specified hand
    // forLeftHand: true for left controller, false for right
    void Start(bool forLeftHand);

    // Update the rotation tracking. Call this each frame while active.
    // deltaTime: frame time for smoothing
    void Update(float deltaTime);

    // Finish the rotation gesture. This "bakes in" the accumulated rotation
    // so that subsequent calls to GetRotationDelta return identity until Start() is called again.
    void Finish();

    // Check if currently active (between Start and Finish)
    bool IsActive() const { return m_isActive; }

    // Get the current (smoothed) rotation delta as a matrix
    // This is the rotation to apply to objects (already inverted)
    RE::NiMatrix3 GetRotationDelta() const { return m_smoothedRotation; }

    // Get the current (smoothed) rotation delta as euler angles
    RE::NiPoint3 GetEulerDelta() const;

    // Smoothing speed (higher = more responsive)
    void SetSmoothingSpeed(float speed) { m_smoothingSpeed = speed; }
    float GetSmoothingSpeed() const { return m_smoothingSpeed; }

    // Get the accumulated (baked) rotation from previous gestures
    // This persists across Start/Finish cycles within the same grab session
    RE::NiMatrix3 GetAccumulatedRotation() const { return m_accumulatedRotation; }
    RE::NiPoint3 GetAccumulatedEulerDelta() const;

    // Reset all state (call when exiting grab mode entirely)
    void Reset();

private:
    // Get the current rotation matrix of the tracked hand
    RE::NiMatrix3 GetHandRotation() const;

    // Calculate the inverse (transpose) of a rotation matrix
    static RE::NiMatrix3 InvertRotation(const RE::NiMatrix3& m);

    // Multiply two rotation matrices
    static RE::NiMatrix3 MultiplyRotations(const RE::NiMatrix3& a, const RE::NiMatrix3& b);

    bool m_isActive = false;
    bool m_forLeftHand = true;

    // Hand rotation when Start() was called
    RE::NiMatrix3 m_startHandRotation;

    // Raw (unsmoothed) rotation delta from hand movement
    RE::NiMatrix3 m_rawRotationDelta;

    // Smoothed rotation delta (what we actually apply)
    RE::NiMatrix3 m_smoothedRotation;

    // Accumulated rotation from all previous gestures in this grab session
    // When Finish() is called, current rotation is "baked in" here
    RE::NiMatrix3 m_accumulatedRotation;

    // Smoothing speed (higher = snappier, lower = smoother)
    float m_smoothingSpeed = 8.0f;
};

} // namespace Grab
