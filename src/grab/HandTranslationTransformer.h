#pragma once

#include "TransformSmoother.h"
#include <RE/Skyrim.h>

namespace Grab {

// HandTranslationTransformer: Transforms VR hand position into object translation offset.
//
// Usage:
// 1. Call Start() when the user begins translation (e.g., trigger pressed)
// 2. Call Update() each frame to get the smoothed translation delta
// 3. Call GetTranslationDelta() to get the current translation offset
// 4. Call Finish() when done - this "bakes in" the translation so it doesn't snap back
//
// Features:
// - Configurable translation scale
// - Smoothed translation using exponential interpolation
// - Tracks cumulative translation across the gesture
// - Additive with other remote placement features
class HandTranslationTransformer
{
public:
    // How much the hand movement is scaled when applied to objects
    static constexpr float TRANSLATION_SCALE = 1.0f;

    HandTranslationTransformer() = default;

    // Start tracking translation from the specified hand
    // forLeftHand: true for left controller, false for right
    void Start(bool forLeftHand);

    // Update the translation tracking. Call this each frame while active.
    // deltaTime: frame time for smoothing
    void Update(float deltaTime);

    // Finish the translation gesture. This "bakes in" the accumulated translation
    // so that subsequent calls to GetTranslationDelta return zero until Start() is called again.
    void Finish();

    // Check if currently active (between Start and Finish)
    bool IsActive() const { return m_isActive; }

    // Get the current (smoothed) translation delta
    // This is the offset to add to object positions
    RE::NiPoint3 GetTranslationDelta() const { return m_smoothedTranslation; }

    // Get the relative offset of the hand from the starting position
    // This is the raw (unsmoothed) translation
    RE::NiPoint3 GetRelativeTranslation() const { return m_rawTranslationDelta; }

    // Smoothing speed (higher = more responsive)
    void SetSmoothingSpeed(float speed) { m_smoothingSpeed = speed; }
    float GetSmoothingSpeed() const { return m_smoothingSpeed; }

    // Get the accumulated (baked) translation from previous gestures
    // This persists across Start/Finish cycles within the same grab session
    RE::NiPoint3 GetAccumulatedTranslation() const { return m_accumulatedTranslation; }

    // Reset all state (call when exiting grab mode entirely)
    void Reset();

private:
    // Get the current position of the tracked hand
    RE::NiPoint3 GetHandPosition() const;

    bool m_isActive = false;
    bool m_forLeftHand = true;

    // Hand position when Start() was called
    RE::NiPoint3 m_startHandPosition;

    // Raw (unsmoothed) translation delta from hand movement
    RE::NiPoint3 m_rawTranslationDelta;

    // Smoothed translation delta (what we actually apply)
    RE::NiPoint3 m_smoothedTranslation;

    // Accumulated translation from all previous gestures in this grab session
    // When Finish() is called, current translation is "baked in" here
    RE::NiPoint3 m_accumulatedTranslation;

    // Smoothing speed (higher = snappier, lower = smoother)
    float m_smoothingSpeed = 8.0f;
};

} // namespace Grab
