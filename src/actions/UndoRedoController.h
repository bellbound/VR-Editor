#pragma once

#include "Action.h"
#include "../IFrameUpdateListener.h"
#include "../EditModeInputManager.h"
#include <chrono>
#include <vector>

namespace Actions {

// UndoRedoController: Handles undo/redo via double-tap gestures
//
// Controls:
// - Double-tap A on right controller = Undo
// - Double-tap B on right controller = Redo
//
// Shows notification when nothing to undo/redo (once per direction change)
class UndoRedoController : public IFrameUpdateListener
{
public:
    static UndoRedoController* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Public undo/redo methods (called by UI)
    void PerformUndo();
    void PerformRedo();

private:
    UndoRedoController() = default;
    ~UndoRedoController() = default;
    UndoRedoController(const UndoRedoController&) = delete;
    UndoRedoController& operator=(const UndoRedoController&) = delete;

    // Input callbacks
    bool OnButtonPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    // Double-tap detection
    void OnButtonTap(vr::EVRButtonId buttonId);
    bool CheckDoubleTap(vr::EVRButtonId buttonId);

    // Apply transform to object (shared by undo/redo)
    // Uses lossy Matrix→Euler conversion (for backward compatibility)
    void ApplyTransform(RE::FormID formId, const RE::NiTransform& transform);

    // Apply transform with explicit Euler angles (lossless)
    // Use this when Euler angles are available to avoid Matrix→Euler conversion errors
    void ApplyTransformWithEuler(RE::FormID formId, const RE::NiTransform& transform,
                                  const RE::NiPoint3& eulerAngles);

    // Apply selection state (shared by undo/redo)
    void ApplySelection(const std::vector<RE::FormID>& formIds);

    // Enable a disabled object (undo delete / redo copy)
    void EnableObject(RE::FormID formId);

    // Disable an object (redo delete / undo copy)
    void DisableObject(RE::FormID formId);

    bool m_initialized = false;

    // Input callback ID
    EditModeInputManager::CallbackId m_buttonCallbackId = EditModeInputManager::InvalidCallbackId;

    // Double-tap timing
    static constexpr float kDoubleTapThreshold = 0.35f;  // 350ms window for double-tap

    // Per-button tap tracking
    struct TapState {
        std::chrono::steady_clock::time_point lastTapTime;
        bool hasPendingTap = false;
    };

    TapState m_aButtonState;  // For undo (A button)
    TapState m_bButtonState;  // For redo (B button)

    // Notification state - show "nothing to undo/redo" only once per direction change
    enum class LastAction { None, Undo, Redo };
    LastAction m_lastAction = LastAction::None;
    bool m_showedNothingToUndo = false;
    bool m_showedNothingToRedo = false;
};

} // namespace Actions
