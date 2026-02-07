#pragma once

#include "Action.h"
#include "ActionHistoryRepository.h"
#include "../selection/SelectionState.h"
#include "../visuals/ObjectHighlighter.h"
#include "../util/Raycast.h"
#include "../util/PositioningUtil.h"
#include "../util/VRNodes.h"
#include "../log.h"
#include <vector>
#include <cmath>
#include <limits>

namespace Actions {

// SnapToGroundHandler: Snaps selected objects to where the player's hand ray hits
// Casts ray from hand in forward direction and places object at hit point
// If ray doesn't hit within 1000 units, objects remain at their default position
class SnapToGroundHandler
{
public:
    static SnapToGroundHandler* GetSingleton()
    {
        static SnapToGroundHandler instance;
        return &instance;
    }

    // Snap all currently selected objects to where the hand ray hits
    // Returns true if any objects were snapped
    bool SnapSelection()
    {
        auto* selectionState = Selection::SelectionState::GetSingleton();
        auto selection = selectionState->GetSelection();

        if (selection.empty()) {
            spdlog::info("SnapToGroundHandler: No objects selected to snap");
            return false;
        }

        // Get hand position and direction for ray cast
        RE::NiPoint3 handPos = GetHandPosition();
        RE::NiPoint3 handForward = GetHandForward();

        // Cast ray from hand in forward direction
        constexpr float kMaxRayDistance = 1000.0f;
        RaycastResult result = Raycast::CastRay(handPos, handForward, kMaxRayDistance);

        // If ray doesn't hit within max distance, don't snap - use default positioning
        if (!result.hit) {
            spdlog::info("SnapToGroundHandler: No surface hit within {} units, skipping snap", kMaxRayDistance);
            return false;
        }

        spdlog::info("SnapToGroundHandler: Hand ray hit at ({:.1f}, {:.1f}, {:.1f}), distance {:.1f}",
            result.hitPoint.x, result.hitPoint.y, result.hitPoint.z, result.distance);

        std::vector<SingleTransform> transforms;
        transforms.reserve(selection.size());

        // Calculate selection center (for multi-object snapping)
        RE::NiPoint3 selectionCenter = CalculateSelectionCenter(selection);

        for (const auto& info : selection) {
            if (!info.ref || !info.ref->Get3D()) continue;

            RE::NiTransform initialTransform = info.ref->Get3D()->world;
            RE::NiPoint3 initialEuler = info.ref->GetAngle();  // Capture from game data
            RE::NiPoint3 objectPos = initialTransform.translate;

            // Calculate this object's offset from the selection center
            RE::NiPoint3 offsetFromCenter = {
                objectPos.x - selectionCenter.x,
                objectPos.y - selectionCenter.y,
                objectPos.z - selectionCenter.z
            };

            // Create new transform at hit point + offset (to maintain relative positions)
            RE::NiTransform newTransform = initialTransform;
            newTransform.translate = {
                result.hitPoint.x + offsetFromCenter.x,
                result.hitPoint.y + offsetFromCenter.y,
                result.hitPoint.z + offsetFromCenter.z
            };

            // LOSSLESS EULER PATH: Compute Euler angles directly from surface normal
            // - Pitch and roll come from the surface normal (how tilted the surface is)
            // - Yaw is PRESERVED from the object's original rotation (no Matrix→Euler conversion!)
            // This eliminates the conversion loss that caused rotation snapping/flipping.
            RE::NiPoint3 surfaceNormal = NormalizeVector(result.hitNormal);
            RE::NiPoint3 changedEuler = PositioningUtil::SurfaceNormalToEuler(surfaceNormal, initialEuler.z);

            // Build rotation matrix from Euler angles for the transform
            newTransform.rotate = EulerToMatrix(changedEuler);

            // Apply the transform
            ApplyTransform(info.ref, newTransform, changedEuler);

            // Record for undo (with Euler angles for lossless undo/redo)
            SingleTransform st;
            st.formId = info.formId;
            st.initialTransform = initialTransform;
            st.changedTransform = newTransform;
            st.initialEulerAngles = initialEuler;
            st.changedEulerAngles = changedEuler;
            transforms.push_back(st);

            spdlog::info("SnapToGroundHandler: Snapped {:08X} to ({:.1f}, {:.1f}, {:.1f})",
                info.formId, newTransform.translate.x, newTransform.translate.y, newTransform.translate.z);
        }

        if (transforms.empty()) {
            spdlog::info("SnapToGroundHandler: No objects could be snapped");
            return false;
        }

        // Record action for undo
        auto* history = ActionHistoryRepository::GetSingleton();

        if (transforms.size() == 1) {
            // Single object - use single transform action
            const auto& t = transforms[0];
            history->AddTransform(t.formId, t.initialTransform, t.changedTransform,
                                  t.initialEulerAngles, t.changedEulerAngles);
            spdlog::info("SnapToGroundHandler: Snapped {:08X} to ground", t.formId);
        } else {
            // Multiple objects - use multi-transform action
            history->AddMultiTransform(std::move(transforms));
            spdlog::info("SnapToGroundHandler: Snapped {} objects to ground", transforms.size());
        }

        // Refresh highlights - ObjectHighlighter will automatically defer if 3D isn't ready yet
        for (const auto& info : selection) {
            selectionState->RefreshHighlightIfSelected(info.ref);
        }
        return true;
    }

private:
    SnapToGroundHandler() = default;
    ~SnapToGroundHandler() = default;
    SnapToGroundHandler(const SnapToGroundHandler&) = delete;
    SnapToGroundHandler& operator=(const SnapToGroundHandler&) = delete;

    // Get the right hand position for ray origin
    RE::NiPoint3 GetHandPosition() const
    {
        RE::NiAVObject* hand = VRNodes::GetRightHand();
        if (hand) {
            return hand->world.translate;
        }
        return RE::NiPoint3{0, 0, 0};
    }

    // Get the right hand forward direction for ray direction
    RE::NiPoint3 GetHandForward() const
    {
        RE::NiAVObject* hand = VRNodes::GetRightHand();
        if (hand) {
            // The hand's forward direction is the Y axis of the rotation matrix
            const RE::NiMatrix3& rot = hand->world.rotate;
            return RE::NiPoint3{
                rot.entry[0][1],
                rot.entry[1][1],
                rot.entry[2][1]
            };
        }
        return RE::NiPoint3{0, 1, 0};
    }

    // Calculate selection center (z = min, x/y = average) for multi-object snapping
    RE::NiPoint3 CalculateSelectionCenter(const std::vector<Selection::SelectionInfo>& selection) const
    {
        if (selection.empty()) {
            return RE::NiPoint3{0, 0, 0};
        }

        float sumX = 0.0f;
        float sumY = 0.0f;
        float minZ = (std::numeric_limits<float>::max)();
        size_t validCount = 0;

        for (const auto& info : selection) {
            if (!info.ref || !info.ref->Get3D()) continue;

            const RE::NiPoint3& pos = info.ref->Get3D()->world.translate;
            sumX += pos.x;
            sumY += pos.y;
            if (pos.z < minZ) {
                minZ = pos.z;
            }
            validCount++;
        }

        if (validCount == 0) {
            return RE::NiPoint3{0, 0, 0};
        }

        float count = static_cast<float>(validCount);
        return RE::NiPoint3{
            sumX / count,  // Average X
            sumY / count,  // Average Y
            minZ           // Minimum Z (floor level)
        };
    }

    // Normalize a vector (return world up if zero)
    RE::NiPoint3 NormalizeVector(const RE::NiPoint3& v)
    {
        float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len > 0.001f) {
            return {v.x / len, v.y / len, v.z / len};
        }
        return {0.0f, 0.0f, 1.0f};  // Default to world up
    }

    // Build rotation matrix from Euler angles (Skyrim's ZYX order)
    RE::NiMatrix3 EulerToMatrix(const RE::NiPoint3& angles)
    {
        float cx = std::cos(angles.x);
        float sx = std::sin(angles.x);
        float cy = std::cos(angles.y);
        float sy = std::sin(angles.y);
        float cz = std::cos(angles.z);
        float sz = std::sin(angles.z);

        RE::NiMatrix3 result;
        // ZYX rotation order: first Z (yaw), then Y (roll), then X (pitch)
        result.entry[0][0] = cy * cz;
        result.entry[0][1] = -cy * sz;
        result.entry[0][2] = sy;

        result.entry[1][0] = sx * sy * cz + cx * sz;
        result.entry[1][1] = -sx * sy * sz + cx * cz;
        result.entry[1][2] = -sx * cy;

        result.entry[2][0] = -cx * sy * cz + sx * sz;
        result.entry[2][1] = cx * sy * sz + sx * cz;
        result.entry[2][2] = cx * cy;

        return result;
    }

    // Apply transform to object using the pre-computed Euler angles (lossless)
    void ApplyTransform(RE::TESObjectREFR* ref, const RE::NiTransform& transform, const RE::NiPoint3& eulerAngles)
    {
        if (!ref) return;

        // Use CommonLib SetPosition/SetAngle which properly persist to game data
        // We pass the Euler angles directly (no Matrix→Euler conversion!)
        ref->SetPosition(transform.translate);
        ref->SetAngle(eulerAngles);

        // Sync the 3D scene node
        ref->Update3DPosition(true);

        // Remove highlight before Disable/Enable destroys 3D - let scheduled refresh reapply it
        ObjectHighlighter::Unhighlight(ref);

        // Disable/Enable cycle to finalize collision
        ref->Disable();
        ref->Enable(false);
    }
};

} // namespace Actions
