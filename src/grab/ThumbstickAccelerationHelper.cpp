#include "ThumbstickAccelerationHelper.h"
#include <cmath>

namespace Grab {

float ThumbstickAccelerationHelper::Update(float stickY, float distance, float deltaTime, const Config& config)
{
    float targetMultiplier = 1.0f;
    float currentDirection = 0.0f;

    if (std::abs(stickY) > 0.01f) {
        currentDirection = (stickY > 0.0f) ? 1.0f : -1.0f;

        // Check if we should accumulate hold time
        bool shouldAccumulate = true;
        if (config.requireFullThrottle) {
            // Standard mode: require near-full throttle to accumulate hold time
            shouldAccumulate = (currentDirection == m_lastYDirection && std::abs(stickY) > config.fullThrottleThreshold);
        } else {
            // NPC mode: accumulate hold time at any input level
            shouldAccumulate = (currentDirection == m_lastYDirection);
        }

        if (shouldAccumulate) {
            m_yHoldDuration += deltaTime;
        } else {
            m_yHoldDuration = deltaTime;
        }

        // Determine if fast mode should be active:
        // - Held for threshold time, OR
        // - Object is far away
        bool shouldUseFastMode = (m_yHoldDuration >= config.fastModeThreshold) ||
                                  (distance > config.farDistanceThreshold);

        if (shouldUseFastMode) {
            // Fast mode conditions:
            // - NOT approaching player while close (within slowdown distance)
            bool isApproaching = (stickY < 0.0f);  // Negative Y = toward player
            bool isTooClose = (distance <= config.slowdownDistance);

            if (isApproaching && isTooClose) {
                // Moving toward player and close - force slow mode
                targetMultiplier = 1.0f;
            } else {
                // Normal fast mode
                targetMultiplier = config.fastMoveMultiplier;
            }
        }
    } else {
        // Input released - reset to normal speed immediately
        m_yHoldDuration = 0.0f;
        currentDirection = 0.0f;
        targetMultiplier = 1.0f;
    }

    m_lastYDirection = currentDirection;

    // Smooth interpolation of speed multiplier
    if (m_currentSpeedMultiplier != targetMultiplier) {
        float transitionSpeed = 1.0f / config.speedTransitionTime;
        if (m_currentSpeedMultiplier < targetMultiplier) {
            m_currentSpeedMultiplier += transitionSpeed * deltaTime;
            if (m_currentSpeedMultiplier > targetMultiplier) {
                m_currentSpeedMultiplier = targetMultiplier;
            }
        } else {
            m_currentSpeedMultiplier -= transitionSpeed * deltaTime;
            if (m_currentSpeedMultiplier < targetMultiplier) {
                m_currentSpeedMultiplier = targetMultiplier;
            }
        }
    }

    return m_currentSpeedMultiplier;
}

void ThumbstickAccelerationHelper::Reset()
{
    m_yHoldDuration = 0.0f;
    m_lastYDirection = 0.0f;
    m_currentSpeedMultiplier = 1.0f;
}

} // namespace Grab
