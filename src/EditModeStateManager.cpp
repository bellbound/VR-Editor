#include "EditModeStateManager.h"
#include "grab/RemoteGrabController.h"
#include "grab/RemoteSelectionController.h"
#include "grab/SphereSelectionController.h"
#include "selection/SelectionState.h"
#include "selection/HoverStateManager.h"
#include "selection/SphereHoverStateManager.h"
#include "visuals/ObjectHighlighter.h"
#include "FrameCallbackDispatcher.h"
#include "interfaces/ThreeDUIInterface001.h"
#include "log.h"
#include "util/SelectionLogger.h"

EditModeStateManager* EditModeStateManager::GetSingleton()
{
    static EditModeStateManager instance;
    return &instance;
}

void EditModeStateManager::Initialize()
{
    if (m_initialized) {
        spdlog::warn("EditModeStateManager already initialized");
        return;
    }

    // Register for frame callbacks (only in edit mode)
    FrameCallbackDispatcher::GetSingleton()->Register(this, true);

    // Register for trigger button presses on right controller
    m_triggerCallbackId = EditModeInputManager::GetSingleton()->AddVrButtonCallback(
        vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger),
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnTriggerPressed(isLeft, isReleased, buttonId);
        }
    );

    // Register for A button on right controller (multi-select modifier)
    m_aButtonCallbackId = EditModeInputManager::GetSingleton()->AddVrButtonCallback(
        vr::ButtonMaskFromId(vr::k_EButton_A),
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnAButtonPressed(isLeft, isReleased, buttonId);
        }
    );

    // Register for grip button on right controller (enter/exit remote placement)
    m_gripCallbackId = EditModeInputManager::GetSingleton()->AddVrButtonCallback(
        vr::ButtonMaskFromId(vr::k_EButton_Grip),
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnGripPressed(isLeft, isReleased, buttonId);
        }
    );

    // Register for joystick click on right controller (toggle selection modes)
    m_joystickCallbackId = EditModeInputManager::GetSingleton()->AddVrButtonCallback(
        vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad),
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnJoystickClicked(isLeft, isReleased, buttonId);
        }
    );

    m_initialized = true;
    spdlog::info("EditModeStateManager initialized");
}

void EditModeStateManager::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Exit any active state
    if (m_state != EditModeState::Idle) {
        EnterIdle();
    }

    // Unregister from frame callbacks
    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    // Unregister input callbacks
    if (m_triggerCallbackId != EditModeInputManager::InvalidCallbackId) {
        EditModeInputManager::GetSingleton()->RemoveVrButtonCallback(m_triggerCallbackId);
        m_triggerCallbackId = EditModeInputManager::InvalidCallbackId;
    }
    if (m_aButtonCallbackId != EditModeInputManager::InvalidCallbackId) {
        EditModeInputManager::GetSingleton()->RemoveVrButtonCallback(m_aButtonCallbackId);
        m_aButtonCallbackId = EditModeInputManager::InvalidCallbackId;
    }
    if (m_gripCallbackId != EditModeInputManager::InvalidCallbackId) {
        EditModeInputManager::GetSingleton()->RemoveVrButtonCallback(m_gripCallbackId);
        m_gripCallbackId = EditModeInputManager::InvalidCallbackId;
    }
    if (m_joystickCallbackId != EditModeInputManager::InvalidCallbackId) {
        EditModeInputManager::GetSingleton()->RemoveVrButtonCallback(m_joystickCallbackId);
        m_joystickCallbackId = EditModeInputManager::InvalidCallbackId;
    }

    m_initialized = false;
    spdlog::info("EditModeStateManager shutdown");
}

bool EditModeStateManager::OnTriggerPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only handle right controller trigger
    if (isLeft) {
        return false;
    }

    // IMPORTANT: In edit mode, we ALWAYS consume trigger input to prevent game actions
    // This callback only fires when edit mode is active (via EditModeInputManager)

    if (isReleased) {
        // Trigger released
        m_triggerHeld = false;
        m_triggerReleasedThisFrame = true;

        switch (m_state) {
            case EditModeState::Idle:
                // Shouldn't normally be in Idle, but if so, enter Selecting
                EnterSelecting();
                break;

            case EditModeState::Selecting:
                // Quick click (released before hold threshold) -> just select
                // If we held long enough, we already entered RemotePlacement in OnFrameUpdate
                if (m_triggerHoldTime < kRemotePlacementHoldTime) {
                    // Validate and select the object that was hovered when trigger was pressed
                    if (m_triggerPressHoverTarget &&
                        m_triggerPressHoverTarget->GetFormID() == m_triggerPressHoverFormId) {

                        auto* selectionState = Selection::SelectionState::GetSingleton();

                        if (m_aButtonHeld) {
                            // Multi-select mode (A held): toggle add/remove
                            spdlog::info("EditModeStateManager: Quick click - multi-select toggle {:08X}",
                                m_triggerPressHoverFormId);
                            selectionState->ToggleSelection(m_triggerPressHoverTarget);
                        } else {
                            // Single-select mode (default): replace selection
                            spdlog::info("EditModeStateManager: Quick click - single-select {:08X}",
                                m_triggerPressHoverFormId);
                            selectionState->SetSingleSelection(m_triggerPressHoverTarget);
                        }
                    } else {
                        spdlog::trace("EditModeStateManager: Quick click but no valid hover target");
                    }
                }
                break;

            case EditModeState::SphereSelecting:
                // Quick click in sphere mode -> select all objects in sphere
                if (m_triggerHoldTime < kRemotePlacementHoldTime) {
                    auto* sphereHoverManager = Selection::SphereHoverStateManager::GetSingleton();
                    if (sphereHoverManager->HasHoveredObjects()) {
                        auto* selectionState = Selection::SelectionState::GetSingleton();
                        const auto& hoveredObjects = sphereHoverManager->GetHoveredObjects();

                        if (m_aButtonHeld) {
                            // Multi-select mode: add all to selection
                            spdlog::info("EditModeStateManager: Sphere quick click - adding {} objects to selection",
                                hoveredObjects.size());
                            for (auto* ref : hoveredObjects) {
                                if (ref && !selectionState->IsSelected(ref)) {
                                    selectionState->AddToSelection(ref);
                                }
                            }
                        } else {
                            // Single-select mode: replace selection with all sphere objects
                            spdlog::info("EditModeStateManager: Sphere quick click - selecting {} objects",
                                hoveredObjects.size());
                            selectionState->ClearAll();
                            for (auto* ref : hoveredObjects) {
                                if (ref) {
                                    selectionState->AddToSelection(ref);
                                }
                            }
                        }
                        SelectionLogger::LogSelectedObjects(hoveredObjects);
                    } else {
                        spdlog::trace("EditModeStateManager: Sphere quick click but no objects in sphere");
                    }
                }
                break;

            case EditModeState::RemotePlacement:
                // Trigger release in RemotePlacement -> finalize and return to previous selection mode
                if (m_enteredRemotePlacementFromHold) {
                    spdlog::info("EditModeStateManager: Trigger released in RemotePlacement, finalizing");
                    Grab::RemoteGrabController::GetSingleton()->OnExit();
                    m_enteredRemotePlacementFromHold = false;
                    // Return to the selection mode we came from
                    if (m_previousSelectionMode == EditModeState::SphereSelecting) {
                        EnterSphereSelecting();
                    } else {
                        EnterSelecting();
                    }
                }
                break;
        }

        // Clear hover target tracking
        m_triggerPressHoverTarget = nullptr;
        m_triggerPressHoverFormId = 0;
        m_triggerPressHitPoint = RE::NiPoint3{0, 0, 0};
        m_triggerHoldTime = 0.0f;
    } else {
        // Trigger pressed
        m_triggerHeld = true;
        m_triggerHoldTime = 0.0f;
        m_triggerPressedThisFrame = true;

        switch (m_state) {
            case EditModeState::Idle:
                // Enter Selecting mode
                EnterSelecting();
                break;

            case EditModeState::Selecting:
                // Record the currently hovered object - we'll use it on release or hold threshold
                if (auto* hoverManager = Selection::HoverStateManager::GetSingleton();
                    hoverManager->HasHoveredObject()) {
                    m_triggerPressHoverTarget = hoverManager->GetHoveredObject();
                    m_triggerPressHoverFormId = m_triggerPressHoverTarget->GetFormID();
                } else {
                    m_triggerPressHoverTarget = nullptr;
                    m_triggerPressHoverFormId = 0;
                }
                break;

            case EditModeState::SphereSelecting:
                // In sphere mode, we don't track a single hover target
                // Selection happens on release or hold threshold based on all objects in sphere
                m_triggerPressHoverTarget = nullptr;
                m_triggerPressHoverFormId = 0;
                m_triggerPressHitPoint = RE::NiPoint3{0, 0, 0};
                break;

            case EditModeState::RemotePlacement:
                // Ignore additional presses in this state
                break;
        }
    }

    // Always consume input in edit mode
    return true;
}

bool EditModeStateManager::OnAButtonPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only handle right controller A button
    if (isLeft) {
        return false;
    }

    // Track A button state for multi-select modifier
    m_aButtonHeld = !isReleased;



    // Don't consume the input - let other systems use A button too
    return false;
}

bool EditModeStateManager::OnGripPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only handle right controller grip
    if (isLeft) {
        return false;
    }

    // Grip button no longer controls remote placement movement
    // Movement is now controlled exclusively via trigger hold (250ms+)
    // Multi-selection movement uses trigger hold while hovering a selected object

    return false;
}

bool EditModeStateManager::OnJoystickClicked(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only handle right controller joystick click
    if (isLeft) {
        return false;
    }

    // Only process on press, not release
    if (isReleased) {
        return false;
    }

    // Toggle between ray selection and sphere selection modes
    switch (m_state) {
        case EditModeState::Selecting:
            spdlog::info("EditModeStateManager: Joystick click - switching to SphereSelecting");
            EnterSphereSelecting();
            return true;

        case EditModeState::SphereSelecting:
            spdlog::info("EditModeStateManager: Joystick click - switching to Selecting");
            EnterSelecting();
            return true;

        case EditModeState::RemotePlacement:
            // Don't switch modes while placing - ignore
            return false;

        default:
            return false;
    }
}

void EditModeStateManager::OnFrameUpdate(float deltaTime)
{
    // Update trigger hold time
    if (m_triggerHeld) {
        m_triggerHoldTime += deltaTime;
    }

    // Clear per-frame flags after processing
    m_triggerPressedThisFrame = false;
    m_triggerReleasedThisFrame = false;

    switch (m_state) {
        case EditModeState::Idle:
            // Nothing to do in idle
            break;

        case EditModeState::Selecting:
            // Check if trigger held long enough to enter RemotePlacement
            if (m_triggerHeld && m_triggerHoldTime >= kRemotePlacementHoldTime) {
                // Validate the hover target is still valid
                if (m_triggerPressHoverTarget &&
                    m_triggerPressHoverTarget->GetFormID() == m_triggerPressHoverFormId) {

                    auto* selectionState = Selection::SelectionState::GetSingleton();

                    // Check if the hovered object is already part of the current selection
                    // If so, move all selected objects together (multi-selection movement)
                    // If not, single-select the hovered object and move it
                    if (selectionState->IsSelected(m_triggerPressHoverTarget)) {
                        // Hovered object is already selected - move entire selection
                        spdlog::info("EditModeStateManager: Hold threshold reached on selected object {:08X} - moving {} selected objects",
                            m_triggerPressHoverFormId, selectionState->GetSelectionCount());
                        // Don't change selection - enter RemotePlacement with current multi-selection
                    } else {
                        // Hovered object is not selected - single-select it
                        spdlog::info("EditModeStateManager: Hold threshold reached - single-select {:08X} and enter RemotePlacement",
                            m_triggerPressHoverFormId);
                        selectionState->SetSingleSelection(m_triggerPressHoverTarget);
                    }

                    // Enter remote placement mode
                    m_enteredRemotePlacementFromHold = true;
                    EnterRemotePlacement();
                } else {
                    // No valid hover target - just clear tracking and stay in Selecting
                    m_triggerPressHoverTarget = nullptr;
                    m_triggerPressHoverFormId = 0;
                    m_triggerPressHitPoint = RE::NiPoint3{0, 0, 0};
                }
            }
            // RemoteSelectionController handles the ray casting and highlighting
            break;

        case EditModeState::SphereSelecting:
            // Check if trigger held long enough to enter RemotePlacement with sphere selection
            if (m_triggerHeld && m_triggerHoldTime >= kRemotePlacementHoldTime) {
                auto* sphereHoverManager = Selection::SphereHoverStateManager::GetSingleton();
                if (sphereHoverManager->HasHoveredObjects()) {
                    auto* selectionState = Selection::SelectionState::GetSingleton();
                    const auto& hoveredObjects = sphereHoverManager->GetHoveredObjects();

                    // Select all objects in sphere and enter RemotePlacement
                    spdlog::info("EditModeStateManager: Sphere hold threshold - selecting {} objects and entering RemotePlacement",
                        hoveredObjects.size());

                    if (m_aButtonHeld) {
                        // Multi-select mode: add all to existing selection
                        for (auto* ref : hoveredObjects) {
                            if (ref && !selectionState->IsSelected(ref)) {
                                selectionState->AddToSelection(ref);
                            }
                        }
                    } else {
                        // Single-select mode: replace selection with all sphere objects
                        selectionState->ClearAll();
                        for (auto* ref : hoveredObjects) {
                            if (ref) {
                                selectionState->AddToSelection(ref);
                            }
                        }
                    }

                    // Enter remote placement mode
                    m_enteredRemotePlacementFromHold = true;
                    EnterRemotePlacement();
                }
            }
            // SphereSelectionController handles the sphere positioning and object scanning
            break;

        case EditModeState::RemotePlacement:
            // RemoteGrabController handles its own updates
            break;
    }
}

void EditModeStateManager::EnterIdle()
{
    EditModeState oldState = m_state;

    // Clean up based on previous state
    if (oldState == EditModeState::Selecting) {
        Grab::RemoteSelectionController::GetSingleton()->StopSelection();
        // StopSelection() already clears HoverStateManager
    } else if (oldState == EditModeState::SphereSelecting) {
        Grab::SphereSelectionController::GetSingleton()->StopSelection();
        // StopSelection() already clears SphereHoverStateManager
    }

    m_state = EditModeState::Idle;
    m_placementRef = nullptr;

    spdlog::info("EditModeStateManager: {} -> Idle", GetStateName(oldState));
}

void EditModeStateManager::EnterSelecting()
{
    EditModeState oldState = m_state;

    // Clean up sphere selection if transitioning from there
    if (oldState == EditModeState::SphereSelecting) {
        Grab::SphereSelectionController::GetSingleton()->StopSelection();
    }

    m_state = EditModeState::Selecting;

    // Start the selection controller for ray casting
    Grab::RemoteSelectionController::GetSingleton()->StartSelection();

    spdlog::info("EditModeStateManager: {} -> Selecting", GetStateName(oldState));
}

void EditModeStateManager::EnterSphereSelecting()
{
    EditModeState oldState = m_state;

    // Clean up ray selection if transitioning from there
    if (oldState == EditModeState::Selecting) {
        Grab::RemoteSelectionController::GetSingleton()->StopSelection();
    }

    m_state = EditModeState::SphereSelecting;

    // Start the sphere selection controller
    Grab::SphereSelectionController::GetSingleton()->StartSelection();

    spdlog::info("EditModeStateManager: {} -> SphereSelecting", GetStateName(oldState));
}

void EditModeStateManager::EnterRemotePlacement()
{
    EditModeState oldState = m_state;

    // Remember which selection mode we came from so we can return to it
    if (oldState == EditModeState::Selecting || oldState == EditModeState::SphereSelecting) {
        m_previousSelectionMode = oldState;
    }

    // Clean up previous state if needed
    if (oldState == EditModeState::Selecting) {
        Grab::RemoteSelectionController::GetSingleton()->StopSelection();
        // StopSelection() already clears HoverStateManager
    } else if (oldState == EditModeState::SphereSelecting) {
        Grab::SphereSelectionController::GetSingleton()->StopSelection();
        // StopSelection() already clears SphereHoverStateManager
    }

    // Clear trigger hover tracking to prevent retriggering hold logic
    m_triggerPressHoverTarget = nullptr;
    m_triggerPressHoverFormId = 0;
    m_triggerPressHitPoint = RE::NiPoint3{0, 0, 0};

    m_state = EditModeState::RemotePlacement;
    m_placementRef = nullptr;  // Not tracking single ref - RemoteGrabController uses SelectionState

    // Tell RemoteGrabController to start - it reads from SelectionState
    Grab::RemoteGrabController::GetSingleton()->OnEnter();

    spdlog::info("EditModeStateManager: {} -> RemotePlacement", GetStateName(oldState));
}

void EditModeStateManager::RequestExitPlacement()
{
    spdlog::info("EditModeStateManager: RequestExitPlacement (from {})", GetStateName(m_state));
    EnterIdle();
}

void EditModeStateManager::ConfirmSelection()
{
    if (m_state != EditModeState::Selecting && m_state != EditModeState::SphereSelecting) {
        return;
    }

    spdlog::info("EditModeStateManager: ConfirmSelection");

    auto* selectionState = Selection::SelectionState::GetSingleton();
    if (!selectionState->HasAnySelection()) {
        spdlog::info("EditModeStateManager: ConfirmSelection but no objects selected");
        EnterIdle();
        return;
    }

    // Enter remote placement mode with current selection (single or multi)
    EnterRemotePlacement();
}

void EditModeStateManager::Cancel()
{
    spdlog::info("EditModeStateManager: Cancel (from {})", GetStateName(m_state));

    switch (m_state) {
        case EditModeState::Selecting:
        case EditModeState::SphereSelecting:
            // Just exit selection mode, keep the selection intact
            EnterIdle();
            break;

        case EditModeState::RemotePlacement:
            // TODO: Revert transform changes for placement
            EnterIdle();
            break;

        default:
            break;
    }
}

void EditModeStateManager::OnEditModeEnter()
{
    spdlog::info("EditModeStateManager: OnEditModeEnter - starting in Selecting mode");
    // Default to Remote Selection Mode when entering edit mode
    EnterSelecting();
}

void EditModeStateManager::OnEditModeExit()
{
    spdlog::info("EditModeStateManager: OnEditModeExit - resetting to Idle");
    // Clean up any active state and return to Idle
    if (m_state != EditModeState::Idle) {
        EnterIdle();
    }

    // Clear all selections (this also removes selection highlights)
    Selection::SelectionState::GetSingleton()->ClearAll();

    // Clear hover states
    Selection::HoverStateManager::GetSingleton()->Clear();
    Selection::SphereHoverStateManager::GetSingleton()->Clear();

    // Clear any remaining highlights as a safety measure
    ObjectHighlighter::UnhighlightAll();
}
