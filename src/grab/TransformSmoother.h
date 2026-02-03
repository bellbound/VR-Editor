#pragma once

#include <RE/N/NiTransform.h>
#include <RE/N/NiPoint3.h>
#include <RE/N/NiMatrix3.h>

namespace Grab {

// Handles smooth interpolation of NiTransform changes over time.
// Uses exponential smoothing for responsive, natural-feeling hand movement.
//
// Key difference from instant transforms:
// - Position smoothly interpolates toward target
// - Rotation smoothly interpolates using quaternion slerp
// - Scale smoothly interpolates
class TransformSmoother {
public:
    TransformSmoother() = default;

    // === Smoothing Speed ===
    // Higher = more responsive (10 = snappy, 2 = floaty)
    void SetSpeed(float speed) { m_speed = speed; }
    float GetSpeed() const { return m_speed; }

    // === Target ===
    void SetTarget(const RE::NiTransform& target);
    const RE::NiTransform& GetTarget() const { return m_target; }

    // === Current Value ===
    const RE::NiTransform& GetCurrent() const { return m_current; }

    // Initialize current value (skips smoothing for first frame)
    void SetCurrent(const RE::NiTransform& current);

    // === Update ===
    // Advance smoothing by deltaTime. Returns true if value changed.
    bool Update(float deltaTime);

    // Check if currently transitioning
    bool IsTransitioning() const { return m_isTransitioning; }

    // Reset to default state
    void Reset();

    // === Static Helpers ===

    // Linearly interpolate between two NiPoint3 values
    static RE::NiPoint3 LerpPosition(const RE::NiPoint3& a, const RE::NiPoint3& b, float t);

    // Interpolate rotation matrices (simplified slerp via quaternion)
    static RE::NiMatrix3 LerpRotation(const RE::NiMatrix3& a, const RE::NiMatrix3& b, float t);

private:
    float m_speed = 10.0f;
    bool m_isTransitioning = false;
    RE::NiTransform m_target;
    RE::NiTransform m_current;
};

} // namespace Grab
