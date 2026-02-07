#pragma once

#include "Action.h"
#include "ActionHistoryRepository.h"
#include "../selection/SelectionState.h"
#include "../visuals/ObjectHighlighter.h"
#include "../log.h"
#include <vector>
#include <cmath>

namespace Actions {

// ResetRotationHandler: Resets rotation of selected objects to identity (0, 0, 0)
// Preserves position and scale, only zeroes out rotation angles
class ResetRotationHandler
{
public:
    static ResetRotationHandler* GetSingleton()
    {
        static ResetRotationHandler instance;
        return &instance;
    }

    // Reset rotation of all currently selected objects
    // Returns true if any objects were reset
    bool ResetSelection()
    {
        auto* selectionState = Selection::SelectionState::GetSingleton();
        auto selection = selectionState->GetSelection();

        if (selection.empty()) {
            spdlog::info("ResetRotationHandler: No objects selected to reset");
            return false;
        }

        std::vector<SingleTransform> transforms;
        transforms.reserve(selection.size());

        for (const auto& info : selection) {
            if (!info.ref || !info.ref->Get3D()) continue;

            RE::NiTransform initialTransform = info.ref->Get3D()->world;
            RE::NiPoint3 initialEuler = info.ref->GetAngle();

            // Target: zero rotation (identity)
            RE::NiPoint3 changedEuler = {0.0f, 0.0f, 0.0f};

            // Create new transform with zeroed rotation
            RE::NiTransform newTransform = initialTransform;
            newTransform.rotate = IdentityMatrix();

            // Apply the transform
            ApplyTransform(info.ref, newTransform, changedEuler);

            // Record for undo
            SingleTransform st;
            st.formId = info.formId;
            st.initialTransform = initialTransform;
            st.changedTransform = newTransform;
            st.initialEulerAngles = initialEuler;
            st.changedEulerAngles = changedEuler;
            transforms.push_back(st);

            spdlog::info("ResetRotationHandler: Reset rotation for {:08X} from ({:.2f}, {:.2f}, {:.2f}) to (0, 0, 0)",
                info.formId, initialEuler.x, initialEuler.y, initialEuler.z);
        }

        if (transforms.empty()) {
            spdlog::info("ResetRotationHandler: No objects could be reset");
            return false;
        }

        // Record action for undo
        auto* history = ActionHistoryRepository::GetSingleton();

        if (transforms.size() == 1) {
            const auto& t = transforms[0];
            history->AddTransform(t.formId, t.initialTransform, t.changedTransform,
                                  t.initialEulerAngles, t.changedEulerAngles);
            spdlog::info("ResetRotationHandler: Reset rotation for {:08X}", t.formId);
        } else {
            history->AddMultiTransform(std::move(transforms));
            spdlog::info("ResetRotationHandler: Reset rotation for {} objects", transforms.size());
        }

        // Refresh highlights - ObjectHighlighter will automatically defer if 3D isn't ready yet
        for (const auto& info : selection) {
            selectionState->RefreshHighlightIfSelected(info.ref);
        }

        return true;
    }

private:
    ResetRotationHandler() = default;
    ~ResetRotationHandler() = default;
    ResetRotationHandler(const ResetRotationHandler&) = delete;
    ResetRotationHandler& operator=(const ResetRotationHandler&) = delete;

    // Return identity rotation matrix
    RE::NiMatrix3 IdentityMatrix()
    {
        RE::NiMatrix3 result;
        result.entry[0][0] = 1.0f; result.entry[0][1] = 0.0f; result.entry[0][2] = 0.0f;
        result.entry[1][0] = 0.0f; result.entry[1][1] = 1.0f; result.entry[1][2] = 0.0f;
        result.entry[2][0] = 0.0f; result.entry[2][1] = 0.0f; result.entry[2][2] = 1.0f;
        return result;
    }

    // Apply transform to object
    void ApplyTransform(RE::TESObjectREFR* ref, const RE::NiTransform& transform, const RE::NiPoint3& eulerAngles)
    {
        if (!ref) return;

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
