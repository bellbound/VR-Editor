#pragma once

namespace Grab {

// Handles time-based acceleration for thumbstick Y-axis movement
// Used by RemoteGrabController to provide smooth speed ramping:
// - Normal speed initially
// - Fast mode after holding for a threshold time OR when object is far away
// - Slowdown when approaching player while close
//
// This class encapsulates the acceleration state and logic that was previously
// duplicated in both NPC mode and standard object mode.
class ThumbstickAccelerationHelper
{
public:
    struct Config {
        float fastModeThreshold = 1.0f;       // Seconds before fast mode activates
        float farDistanceThreshold = 800.0f;  // Distance beyond which fast mode is always active
        float slowdownDistance = 130.0f;      // Distance at which fast mode disables when approaching
        float fastMoveMultiplier = 3.0f;      // Speed multiplier for fast mode
        float speedTransitionTime = 0.15f;    // Seconds to interpolate between speeds
        float fullThrottleThreshold = 0.92f;  // Y-axis value required to accumulate hold time (standard mode)
        bool requireFullThrottle = true;      // If true, only accumulate hold time at full throttle
    };

    // Update acceleration state and return the current speed multiplier
    // Parameters:
    //   stickY: Thumbstick Y value after deadzone processing (-1 to 1, 0 = neutral)
    //   distance: Current distance from player to object
    //   deltaTime: Frame time in seconds
    //   config: Configuration parameters
    // Returns: Speed multiplier to apply (1.0 = normal, higher = faster)
    float Update(float stickY, float distance, float deltaTime, const Config& config);

    // Reset all acceleration state (call when exiting grab mode)
    void Reset();

    // Get current speed multiplier without updating
    float GetCurrentMultiplier() const { return m_currentSpeedMultiplier; }

    // Get how long Y-axis has been held in the same direction
    float GetHoldDuration() const { return m_yHoldDuration; }

private:
    float m_yHoldDuration = 0.0f;           // How long Y-axis has been held in same direction
    float m_lastYDirection = 0.0f;          // Last Y direction: -1 (toward), 0 (neutral), +1 (away)
    float m_currentSpeedMultiplier = 1.0f;  // Current interpolated speed multiplier
};

} // namespace Grab
