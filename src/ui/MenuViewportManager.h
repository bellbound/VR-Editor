#pragma once

#include <RE/Skyrim.h>
#include "../util/VRNodes.h"
#include "../log.h"
#include "openvr.h"
#include "../../external/VRHookAPI.h"
#include <cmath>

namespace UI {

// MenuViewportManager: Manages menu visibility based on HMD viewing direction
//
// Approach:
// - Vector 1: HMD forward direction (where the user is looking) - from OpenVR
// - Vector 2: Direction from HMD position to menu position
// - Use dot product to check alignment between these vectors
// - Menu is visible when angle between vectors is less than maxViewAngleDegrees
//
class MenuViewportManager
{
public:
    // Maximum angle (degrees) between look direction and menu direction for menu to show
    static constexpr float kDefaultMaxViewAngleDegrees = 30.0f;

    MenuViewportManager() = default;

    void SetMaxViewAngle(float degrees) { m_maxViewAngleDegrees = degrees; }
    float GetMaxViewAngle() const { return m_maxViewAngleDegrees; }

    // Call this every frame - internally tracks frame count for interval checking
    // Returns true if visibility state changed
    bool Update(const RE::NiPoint3& menuWorldPosition)
    {
        m_frameCounter++;

        // Only check every N frames to reduce overhead
        constexpr int kCheckIntervalFrames = 10;
        if (m_frameCounter < kCheckIntervalFrames) {
            return false;
        }
        m_frameCounter = 0;

        // Get OpenVR system for HMD pose
        if (!m_vrSystem) {
            auto* hookManager = RequestOpenVRHookManagerObject();
            if (hookManager) {
                m_vrSystem = hookManager->GetVRSystem();
            }
            if (!m_vrSystem) {
                return false;
            }
        }

        // Get HMD pose from OpenVR (device index 0 is always HMD)
        vr::TrackedDevicePose_t poses[1];
        m_vrSystem->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseStanding, 0.0f, poses, 1);

        if (!poses[0].bPoseIsValid) {
            return false;
        }

        // Extract HMD position and forward direction from pose matrix
        // OpenVR matrix is row-major: m[row][col]
        // Position is in the last column (index 3)
        // Forward is -Z axis (third row, negated)
        const auto& m = poses[0].mDeviceToAbsoluteTracking.m;

        // HMD position (last column)
        RE::NiPoint3 hmdPos = {m[0][3], m[1][3], m[2][3]};

        // HMD forward direction (-Z axis in OpenVR)
        RE::NiPoint3 hmdForward = {-m[0][2], -m[1][2], -m[2][2]};

        // Vector 2: Direction from HMD position to menu position
        RE::NiPoint3 toMenu = {
            menuWorldPosition.x - hmdPos.x,
            menuWorldPosition.y - hmdPos.y,
            menuWorldPosition.z - hmdPos.z
        };

        // Normalize toMenu vector
        float toMenuLen = std::sqrt(toMenu.x * toMenu.x + toMenu.y * toMenu.y + toMenu.z * toMenu.z);
        if (toMenuLen < 0.001f) {
            // Menu is at HMD position - always visible
            return false;
        }
        toMenu.x /= toMenuLen;
        toMenu.y /= toMenuLen;
        toMenu.z /= toMenuLen;

        // Dot product gives cosine of angle between vectors
        float dot = hmdForward.x * toMenu.x + hmdForward.y * toMenu.y + hmdForward.z * toMenu.z;

        // Clamp to valid range for acos
        dot = std::clamp(dot, -1.0f, 1.0f);

        // Convert to degrees
        float angleDegrees = std::acos(dot) * (180.0f / 3.14159265f);

        // Determine if menu should be visible
        // Use hysteresis: slightly tighter threshold to expand than to collapse
        constexpr float kHysteresis = 5.0f;
        bool wasExpanded = m_isExpanded;

        if (m_isExpanded) {
            // Currently visible - hide if looking away
            if (angleDegrees > m_maxViewAngleDegrees) {
                m_isExpanded = false;
                spdlog::info("MenuViewportManager: COLLAPSE - angle {:.1f} > {:.1f} deg | hmdFwd=({:.2f},{:.2f},{:.2f}) toMenu=({:.2f},{:.2f},{:.2f})",
                    angleDegrees, m_maxViewAngleDegrees,
                    hmdForward.x, hmdForward.y, hmdForward.z,
                    toMenu.x, toMenu.y, toMenu.z);
            }
        } else {
            // Currently hidden - show if looking at menu
            if (angleDegrees < (m_maxViewAngleDegrees - kHysteresis)) {
                m_isExpanded = true;
                spdlog::info("MenuViewportManager: EXPAND - angle {:.1f} < {:.1f} deg | hmdFwd=({:.2f},{:.2f},{:.2f}) toMenu=({:.2f},{:.2f},{:.2f})",
                    angleDegrees, m_maxViewAngleDegrees - kHysteresis,
                    hmdForward.x, hmdForward.y, hmdForward.z,
                    toMenu.x, toMenu.y, toMenu.z);
            }
        }

        return (wasExpanded != m_isExpanded);
    }

    // Returns true if menu should be in expanded (full) state
    bool IsExpanded() const { return m_isExpanded; }

    // Returns true if menu should be in collapsed (orb only) state
    bool IsCollapsed() const { return !m_isExpanded; }

    // Reset to expanded state (call when menu is first opened)
    void Reset()
    {
        m_isExpanded = true;
        m_frameCounter = 0;
    }

private:
    vr::IVRSystem* m_vrSystem = nullptr;
    float m_maxViewAngleDegrees = kDefaultMaxViewAngleDegrees;
    bool m_isExpanded = true;
    int m_frameCounter = 0;
};

} // namespace UI
