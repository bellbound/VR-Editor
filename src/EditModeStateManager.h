#pragma once

#include "IFrameUpdateListener.h"
#include "EditModeInputManager.h"
#include <RE/Skyrim.h>

// EditModeStateManager: Central coordinator for edit mode interactions
//
// States:
// - Idle: Not doing anything (rarely used - edit mode starts in Selecting)
// - Selecting: Ray-based selection mode - point at objects to highlight
// - RemotePlacement: Object at distance, controlled by thumbstick
//
// Trigger Behavior (in Selecting state):
// - Quick click (release before 250ms): Select the hovered object (single-select)
// - Click and hold (250ms+): Select the hovered object AND enter RemotePlacement
//   - If hovering an object that's already part of a multi-selection, move all selected objects
// - Release trigger in RemotePlacement: Finalize placement, return to Selecting
//
// Selection behavior:
// - Default (A not held): Single-select mode - selecting clears previous selection
// - Hold A button: Multi-select mode - toggle add/remove from selection
//
// Hover state:
// - Hover tracking is managed by HoverStateManager (single source of truth)

enum class EditModeState {
    Idle,              // Not doing anything
    Selecting,         // Ray-based selection mode - click to toggle, hold to exit
    SphereSelecting,   // Volume-based selection mode - select all objects in sphere
    RemotePlacement    // Object at distance, controlled by thumbstick
};

class EditModeStateManager : public IFrameUpdateListener
{
public:
    static EditModeStateManager* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Current state
    EditModeState GetState() const { return m_state; }
    bool IsIdle() const { return m_state == EditModeState::Idle; }
    bool IsSelecting() const { return m_state == EditModeState::Selecting; }
    bool IsSphereSelecting() const { return m_state == EditModeState::SphereSelecting; }
    bool IsPlacing() const { return m_state == EditModeState::RemotePlacement; }
    bool IsInAnySelectionMode() const { return m_state == EditModeState::Selecting || m_state == EditModeState::SphereSelecting; }

    // Get the object being actively manipulated (if in placement mode)
    RE::TESObjectREFR* GetPlacementObject() const { return m_placementRef; }

    // State transition request (called by sub-controllers)
    void RequestExitPlacement();

    // Called to confirm selection and enter placement with selected object(s)
    void ConfirmSelection();

    // Cancel current operation and return to Idle
    void Cancel();

    // Called when entering/exiting edit mode to set initial state
    void OnEditModeEnter();
    void OnEditModeExit();

    // Timing constants
    static constexpr float kRemotePlacementHoldTime = 0.250f;  // 250ms hold to enter remote placement

private:
    EditModeStateManager() = default;
    ~EditModeStateManager() = default;
    EditModeStateManager(const EditModeStateManager&) = delete;
    EditModeStateManager& operator=(const EditModeStateManager&) = delete;

    // Input callbacks
    bool OnTriggerPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);
    bool OnAButtonPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);
    bool OnGripPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);
    bool OnJoystickClicked(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    // State transition implementations
    void EnterIdle();
    void EnterSelecting();
    void EnterSphereSelecting();
    void EnterRemotePlacement();  // Uses current SelectionState

    bool m_initialized = false;
    EditModeState m_state = EditModeState::Idle;

    // Object being placed (in HandPlacement or RemotePlacement)
    RE::TESObjectREFR* m_placementRef = nullptr;
    RE::NiTransform m_initialTransform;

    // Trigger state tracking
    bool m_triggerHeld = false;
    float m_triggerHoldTime = 0.0f;
    bool m_triggerPressedThisFrame = false;
    bool m_triggerReleasedThisFrame = false;
    bool m_enteredRemotePlacementFromHold = false;  // True if we entered RemotePlacement via trigger hold
    RE::TESObjectREFR* m_triggerPressHoverTarget = nullptr;  // Object hovered when trigger was pressed
    RE::FormID m_triggerPressHoverFormId = 0;  // FormID for validation
    RE::NiPoint3 m_triggerPressHitPoint{0, 0, 0};  // Ray hit point when trigger was pressed (for grab offset)

    // A button state - when held, enables multi-select mode
    bool m_aButtonHeld = false;

    // Track which selection mode to return to after remote placement
    EditModeState m_previousSelectionMode = EditModeState::Selecting;

    // Input callback IDs
    EditModeInputManager::CallbackId m_triggerCallbackId = EditModeInputManager::InvalidCallbackId;
    EditModeInputManager::CallbackId m_aButtonCallbackId = EditModeInputManager::InvalidCallbackId;
    EditModeInputManager::CallbackId m_gripCallbackId = EditModeInputManager::InvalidCallbackId;
    EditModeInputManager::CallbackId m_joystickCallbackId = EditModeInputManager::InvalidCallbackId;
};

// Helper to get state name for logging
inline const char* GetStateName(EditModeState state) {
    switch (state) {
        case EditModeState::Idle: return "Idle";
        case EditModeState::Selecting: return "Selecting";
        case EditModeState::SphereSelecting: return "SphereSelecting";
        case EditModeState::RemotePlacement: return "RemotePlacement";
        default: return "Unknown";
    }
}
