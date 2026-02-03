#pragma once

#include "IFrameUpdateListener.h"
#include "util/InputManager.h"
#include <chrono>

// Handles transitioning into/out of edit mode
// Detection: Player shoves hand inside an object and double-taps trigger
//
// How it works:
// 1. Cast ray from HMD to hand - if hits wall before reaching hand, suspect we're inside object
// 2. Cast reverse ray from hand to HMD - if doesn't hit anything, confirm (backface culling)
// 3. If both checks pass and trigger is double-tapped, toggle edit mode
class EditModeTransitioner : public IFrameUpdateListener
{
public:
    static EditModeTransitioner* GetSingleton();

    // Initialize - registers for frame callbacks and input events
    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

private:
    EditModeTransitioner() = default;
    ~EditModeTransitioner() = default;
    EditModeTransitioner(const EditModeTransitioner&) = delete;
    EditModeTransitioner& operator=(const EditModeTransitioner&) = delete;

    // Check if hand is inside an object using bidirectional raycast
    // Returns true if hand appears to be inside geometry
    bool IsHandInsideObject(bool isLeft);

    // Input callback for trigger press
    bool OnTriggerPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    bool m_initialized = false;

    // Input callback ID
    InputManager::CallbackId m_triggerCallbackId = InputManager::InvalidCallbackId;

    // Double-tap detection (simplified - no target tracking needed)
    float m_doubleTapThreshold = 0.4f;  // seconds
    std::chrono::steady_clock::time_point m_lastTriggerTime;
    bool m_lastTriggerWasInsideObject = false;
    bool m_hasLastTrigger = false;

    // Track which hand was used for last trigger (for double-tap to work)
    bool m_lastTriggerIsLeft = false;
};
