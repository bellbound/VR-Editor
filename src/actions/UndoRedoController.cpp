#include "UndoRedoController.h"
#include "ActionHistoryRepository.h"
#include "../FrameCallbackDispatcher.h"
#include "../selection/SelectionState.h"
#include "../util/NotificationManager.h"
#include "../util/PositioningUtil.h"
#include "../persistence/ChangedObjectRegistry.h"
#include "../log.h"
#include <cmath>

namespace {
    // Get a display name for a form (editor ID or fallback to form ID hex)
    std::string GetFormDisplayName(RE::FormID formId) {
        auto* form = RE::TESForm::LookupByID(formId);
        if (!form) {
            return fmt::format("{:08X}", formId);
        }

        // Try to get editor ID
        const char* editorId = form->GetFormEditorID();
        if (editorId && editorId[0] != '\0') {
            return std::string(editorId);
        }

        // Try to get the base object's editor ID if this is a reference
        if (auto* ref = form->As<RE::TESObjectREFR>()) {
            if (auto* baseObj = ref->GetBaseObject()) {
                editorId = baseObj->GetFormEditorID();
                if (editorId && editorId[0] != '\0') {
                    return std::string(editorId);
                }
            }
        }

        // Fallback to hex form ID
        return fmt::format("{:08X}", formId);
    }
} // anonymous namespace

namespace Actions {

UndoRedoController* UndoRedoController::GetSingleton()
{
    static UndoRedoController instance;
    return &instance;
}

void UndoRedoController::Initialize()
{
    if (m_initialized) {
        spdlog::warn("UndoRedoController already initialized");
        return;
    }

    // Register for frame callbacks (only in edit mode)
    FrameCallbackDispatcher::GetSingleton()->Register(this, true);

    // Register for A and B button presses on right controller
    // A = k_EButton_A (7), B = k_EButton_ApplicationMenu (1) on most VR controllers
    uint64_t buttonMask = vr::ButtonMaskFromId(vr::k_EButton_A) |
                          vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu);

    m_buttonCallbackId = EditModeInputManager::GetSingleton()->AddVrButtonCallback(
        buttonMask,
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnButtonPressed(isLeft, isReleased, buttonId);
        }
    );

    m_initialized = true;
    spdlog::info("UndoRedoController initialized");
}

void UndoRedoController::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Unregister from frame callbacks
    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    // Unregister input callback
    if (m_buttonCallbackId != EditModeInputManager::InvalidCallbackId) {
        EditModeInputManager::GetSingleton()->RemoveVrButtonCallback(m_buttonCallbackId);
        m_buttonCallbackId = EditModeInputManager::InvalidCallbackId;
    }

    m_initialized = false;
    spdlog::info("UndoRedoController shutdown");
}

void UndoRedoController::OnFrameUpdate(float /*deltaTime*/)
{
    // Currently no per-frame work needed
    // Double-tap detection is handled in button callbacks
}

bool UndoRedoController::OnButtonPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only respond to right controller
    if (isLeft) {
        return false;
    }

    // Only detect on button release (tap completed)
    if (!isReleased) {
        return false;
    }

    // Check for double-tap
    if (buttonId == vr::k_EButton_A) {
        OnButtonTap(buttonId);
        if (CheckDoubleTap(buttonId)) {
            PerformUndo();
            return true;  // Consume input
        }
    } else if (buttonId == vr::k_EButton_ApplicationMenu) {
        OnButtonTap(buttonId);
        if (CheckDoubleTap(buttonId)) {
            PerformRedo();
            return true;  // Consume input
        }
    }

    return false;  // Don't consume single taps
}

void UndoRedoController::OnButtonTap(vr::EVRButtonId buttonId)
{
    auto now = std::chrono::steady_clock::now();

    TapState* state = nullptr;
    if (buttonId == vr::k_EButton_A) {
        state = &m_aButtonState;
    } else if (buttonId == vr::k_EButton_ApplicationMenu) {
        state = &m_bButtonState;
    }

    if (state) {
        state->lastTapTime = now;
        state->hasPendingTap = true;
    }
}

bool UndoRedoController::CheckDoubleTap(vr::EVRButtonId buttonId)
{
    auto now = std::chrono::steady_clock::now();

    TapState* state = nullptr;
    if (buttonId == vr::k_EButton_A) {
        state = &m_aButtonState;
    } else if (buttonId == vr::k_EButton_ApplicationMenu) {
        state = &m_bButtonState;
    }

    if (!state || !state->hasPendingTap) {
        return false;
    }

    // Check if this tap is within double-tap window of the previous tap
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state->lastTapTime);

    // Note: We need to track two taps. The first tap sets hasPendingTap.
    // The second tap (if within threshold) triggers the action.
    // After triggering, we clear the pending tap.

    // This is the second tap - check timing
    // Actually, our logic is: each tap updates lastTapTime and sets hasPendingTap.
    // To detect double-tap, we need to see if time since PREVIOUS tap is short enough.
    // Let's restructure: track lastTapTime and check if current tap is within threshold.

    // Revised logic: we need a "previous" tap time to compare against
    // Let me simplify: store last tap time, on each tap check if elapsed < threshold
    // If yes, it's a double-tap, clear state. If no, update time for next potential.

    static std::chrono::steady_clock::time_point s_prevATapTime;
    static std::chrono::steady_clock::time_point s_prevBTapTime;
    static bool s_hasATap = false;
    static bool s_hasBTap = false;

    if (buttonId == vr::k_EButton_A) {
        if (s_hasATap) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - s_prevATapTime);
            if (elapsed.count() < kDoubleTapThreshold * 1000) {
                s_hasATap = false;  // Reset after double-tap
                spdlog::trace("UndoRedoController: Double-tap A detected ({}ms)", elapsed.count());
                return true;
            }
        }
        s_prevATapTime = now;
        s_hasATap = true;
    } else if (buttonId == vr::k_EButton_ApplicationMenu) {
        if (s_hasBTap) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - s_prevBTapTime);
            if (elapsed.count() < kDoubleTapThreshold * 1000) {
                s_hasBTap = false;  // Reset after double-tap
                spdlog::trace("UndoRedoController: Double-tap B detected ({}ms)", elapsed.count());
                return true;
            }
        }
        s_prevBTapTime = now;
        s_hasBTap = true;
    }

    return false;
}

void UndoRedoController::PerformUndo()
{
    auto* repo = ActionHistoryRepository::GetSingleton();
    auto* notif = NotificationManager::GetSingleton();

    // Check for user-visible actions (skips single-select which is internal-only)
    if (!repo->HasUserVisibleUndo()) {
        // Show notification only once until redo is performed
        if (!m_showedNothingToUndo) {
            notif->Show("Nothing to undo");
            m_showedNothingToUndo = true;
            spdlog::info("UndoRedoController: Nothing to undo");
        }
        return;
    }

    // Loop until we find and process a user-visible action
    // Single-select actions are applied internally but not shown to user
    while (repo->CanUndo()) {
        auto action = repo->Undo();
        if (!action) {
            break;
        }

        // Notify ChangedObjectRegistry that this action was undone
        // If this was the first change to an object (and created this session),
        // the registry entry will be removed
        Util::ActionId undoneId = GetActionId(*action);
        Persistence::ChangedObjectRegistry::GetSingleton()->OnActionUndone(undoneId);

        // Check if this action is user-visible
        bool isUserVisible = IsUserVisibleAction(*action);

        // Process the undone action - apply the INITIAL state (reverse the change)
        std::visit([this, notif, isUserVisible](const auto& act) {
            using T = std::decay_t<decltype(act)>;
            if constexpr (std::is_same_v<T, TransformAction>) {
                // Use Euler angles directly for lossless undo
                ApplyTransformWithEuler(act.formId, act.initialTransform, act.initialEulerAngles);

                std::string name = GetFormDisplayName(act.formId);
                notif->Show("Undo: Move {}", name);
                spdlog::info("UndoRedoController: Undid transform for {} ({:08X})", name, act.formId);
            } else if constexpr (std::is_same_v<T, MultiTransformAction>) {
                // Undo all transforms in the group
                for (const auto& st : act.transforms) {
                    // Use Euler angles directly for lossless undo
                    ApplyTransformWithEuler(st.formId, st.initialTransform, st.initialEulerAngles);
                }

                notif->Show("Undo: Move {} objects", act.transforms.size());
                spdlog::info("UndoRedoController: Undid multi-transform for {} objects", act.transforms.size());
            } else if constexpr (std::is_same_v<T, SelectionAction>) {
                // Always apply selection for state consistency
                ApplySelection(act.previousSelection);

                // Only show notification for multi-select actions
                if (isUserVisible) {
                    notif->Show("Undo: Selection");
                    spdlog::info("UndoRedoController: Undid selection change ({} -> {} items)",
                        act.newSelection.size(), act.previousSelection.size());
                } else {
                    spdlog::trace("UndoRedoController: Undid single-select (internal, no notification)");
                }
            } else if constexpr (std::is_same_v<T, DeleteAction>) {
                // Undo delete = Enable the disabled objects
                for (const auto& del : act.deletedObjects) {
                    EnableObject(del.formId);
                }

                notif->Show("Undo: Delete {} objects", act.deletedObjects.size());
                spdlog::info("UndoRedoController: Undid deletion of {} objects (re-enabled)", act.deletedObjects.size());
            } else if constexpr (std::is_same_v<T, CopyAction>) {
                // Undo copy = Disable the created copies
                for (const auto& copy : act.copiedObjects) {
                    DisableObject(copy.createdFormId);
                }

                notif->Show("Undo: Duplicate {} objects", act.copiedObjects.size());
                spdlog::info("UndoRedoController: Undid copy of {} objects (disabled)", act.copiedObjects.size());
            }
        }, *action);

        // If this was a user-visible action, we're done
        // Otherwise, continue looping to find the next user-visible action
        if (isUserVisible) {
            break;
        }
    }

    // Reset notification state
    m_lastAction = LastAction::Undo;
    m_showedNothingToRedo = false;  // Allow redo notification again after an undo
}

void UndoRedoController::PerformRedo()
{
    auto* repo = ActionHistoryRepository::GetSingleton();
    auto* notif = NotificationManager::GetSingleton();

    if (!repo->CanRedo()) {
        // Show notification only once until undo is performed
        if (!m_showedNothingToRedo) {
            notif->Show("Nothing to redo");
            m_showedNothingToRedo = true;
            spdlog::info("UndoRedoController: Nothing to redo");
        }
        return;
    }

    auto action = repo->Redo();
    if (!action) {
        return;
    }

    // Process the redone action - apply the CHANGED state (re-apply the change)
    std::visit([this, notif](const auto& act) {
        using T = std::decay_t<decltype(act)>;
        if constexpr (std::is_same_v<T, TransformAction>) {
            // Use Euler angles directly for lossless redo
            ApplyTransformWithEuler(act.formId, act.changedTransform, act.changedEulerAngles);

            std::string name = GetFormDisplayName(act.formId);
            notif->Show("Redo: Move {}", name);
            spdlog::info("UndoRedoController: Redid transform for {} ({:08X})", name, act.formId);
        } else if constexpr (std::is_same_v<T, MultiTransformAction>) {
            // Redo all transforms in the group
            for (const auto& st : act.transforms) {
                // Use Euler angles directly for lossless redo
                ApplyTransformWithEuler(st.formId, st.changedTransform, st.changedEulerAngles);
            }

            notif->Show("Redo: Move {} objects", act.transforms.size());
            spdlog::info("UndoRedoController: Redid multi-transform for {} objects", act.transforms.size());
        } else if constexpr (std::is_same_v<T, SelectionAction>) {
            ApplySelection(act.newSelection);

            notif->Show("Redo: Selection");
            spdlog::info("UndoRedoController: Redid selection change ({} -> {} items)",
                act.previousSelection.size(), act.newSelection.size());
        } else if constexpr (std::is_same_v<T, DeleteAction>) {
            // Redo delete = Disable the objects again
            for (const auto& del : act.deletedObjects) {
                DisableObject(del.formId);
            }

            notif->Show("Redo: Delete {} objects", act.deletedObjects.size());
            spdlog::info("UndoRedoController: Redid deletion of {} objects (disabled)", act.deletedObjects.size());
        } else if constexpr (std::is_same_v<T, CopyAction>) {
            // Redo copy = Enable the copies again
            for (const auto& copy : act.copiedObjects) {
                EnableObject(copy.createdFormId);
            }

            notif->Show("Redo: Duplicate {} objects", act.copiedObjects.size());
            spdlog::info("UndoRedoController: Redid copy of {} objects (re-enabled)", act.copiedObjects.size());
        }
    }, *action);

    // Reset notification state
    m_lastAction = LastAction::Redo;
    m_showedNothingToUndo = false;  // Allow undo notification again after a redo
}

void UndoRedoController::ApplyTransform(RE::FormID formId, const RE::NiTransform& transform)
{
    // Look up the reference by FormID
    auto* form = RE::TESForm::LookupByID(formId);
    if (!form) {
        spdlog::warn("UndoRedoController: Could not find form {:08X}", formId);
        return;
    }

    auto* ref = form->As<RE::TESObjectREFR>();
    if (!ref) {
        spdlog::warn("UndoRedoController: Form {:08X} is not a reference", formId);
        return;
    }

    // Convert rotation matrix to Euler angles (LOSSY - may cause rotation snapping)
    RE::NiPoint3 angles = PositioningUtil::MatrixToEulerAngles(transform.rotate);

    // Use native engine functions which properly sync both game data AND 3D node rotation
    // CommonLib's SetAngle only updates game data; the native function also syncs the 3D node
    // This is critical because Update3DPosition only syncs position, not rotation!
    PositioningUtil::SetPositionNative(ref, transform.translate);
    PositioningUtil::SetAngleNative(ref, angles);

    // Apply scale from the transform
    ref->SetScale(transform.scale);

    // Sync the 3D scene node with the new game data position
    ref->Update3DPosition(true);

    // Disable/Enable cycle forces Havok to rebuild collision at new position
    // This is necessary to finalize position for static objects
    ref->Disable();
    ref->Enable(false);  // false = don't reset inventory

    // Reapply highlighting if this object is still selected
    // (Enable recreates the 3D which destroys any highlight state)
    Selection::SelectionState::GetSingleton()->RefreshHighlightIfSelected(ref);

    spdlog::trace("UndoRedoController: Applied transform to {:08X} (lossy Matrix→Euler, scale={:.3f})",
        formId, transform.scale);
}

void UndoRedoController::ApplyTransformWithEuler(RE::FormID formId, const RE::NiTransform& transform,
                                                   const RE::NiPoint3& eulerAngles)
{
    // Look up the reference by FormID
    auto* form = RE::TESForm::LookupByID(formId);
    if (!form) {
        spdlog::warn("UndoRedoController: Could not find form {:08X}", formId);
        return;
    }

    auto* ref = form->As<RE::TESObjectREFR>();
    if (!ref) {
        spdlog::warn("UndoRedoController: Form {:08X} is not a reference", formId);
        return;
    }

    // Use the provided Euler angles directly (LOSSLESS - no Matrix→Euler conversion)
    PositioningUtil::SetPositionNative(ref, transform.translate);
    PositioningUtil::SetAngleNative(ref, eulerAngles);

    // Apply scale from the transform
    ref->SetScale(transform.scale);

    // Sync the 3D scene node with the new game data position
    ref->Update3DPosition(true);

    // Disable/Enable cycle forces Havok to rebuild collision at new position
    ref->Disable();
    ref->Enable(false);  // false = don't reset inventory

    // Reapply highlighting if this object is still selected
    Selection::SelectionState::GetSingleton()->RefreshHighlightIfSelected(ref);

    spdlog::trace("UndoRedoController: Applied transform to {:08X} (lossless Euler, scale={:.3f})",
        formId, transform.scale);
}

void UndoRedoController::ApplySelection(const std::vector<RE::FormID>& formIds)
{
    auto* selState = Selection::SelectionState::GetSingleton();

    // Suppress callback during undo/redo to avoid recording the change we're restoring
    selState->SetSuppressCallback(true);

    // Clear current selection
    selState->ClearAll();

    // Add each form to selection
    for (RE::FormID formId : formIds) {
        auto* form = RE::TESForm::LookupByID(formId);
        if (!form) {
            spdlog::warn("UndoRedoController: Could not find form {:08X} for selection restore", formId);
            continue;
        }

        auto* ref = form->As<RE::TESObjectREFR>();
        if (!ref) {
            spdlog::warn("UndoRedoController: Form {:08X} is not a reference for selection restore", formId);
            continue;
        }

        if (formIds.size() == 1) {
            selState->SetSingleSelection(ref);
        } else {
            selState->AddToSelection(ref);
        }
    }

    // Re-enable callback
    selState->SetSuppressCallback(false);

    spdlog::trace("UndoRedoController: Applied selection with {} items", formIds.size());
}

void UndoRedoController::EnableObject(RE::FormID formId)
{
    auto* form = RE::TESForm::LookupByID(formId);
    if (!form) {
        spdlog::warn("UndoRedoController: Could not find form {:08X} for enable", formId);
        return;
    }

    auto* ref = form->As<RE::TESObjectREFR>();
    if (!ref) {
        spdlog::warn("UndoRedoController: Form {:08X} is not a reference for enable", formId);
        return;
    }

    // Enable the object - makes it visible and interactive again
    ref->Enable(false);  // false = don't reset inventory

    spdlog::trace("UndoRedoController: Enabled object {:08X}", formId);
}

void UndoRedoController::DisableObject(RE::FormID formId)
{
    auto* form = RE::TESForm::LookupByID(formId);
    if (!form) {
        spdlog::warn("UndoRedoController: Could not find form {:08X} for disable", formId);
        return;
    }

    auto* ref = form->As<RE::TESObjectREFR>();
    if (!ref) {
        spdlog::warn("UndoRedoController: Form {:08X} is not a reference for disable", formId);
        return;
    }

    // Disable the object - hides it but keeps it in the game
    ref->Disable();

    spdlog::trace("UndoRedoController: Disabled object {:08X}", formId);
}

} // namespace Actions
