#pragma once
#include "util/InputManager.h"
#include <functional>
#include <vector>

// Input manager that only dispatches events when in edit mode
// Wraps InputManager and filters callbacks based on EditModeManager::IsInEditMode()
class EditModeInputManager
{
public:
    // Return true to consume/block the input, false to let it pass through
    using VrButtonCallback = std::function<bool(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)>;
    using CallbackId = uint32_t;
    static constexpr CallbackId InvalidCallbackId = 0;

    static EditModeInputManager* GetSingleton();

    // Call after InputManager is initialized
    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // Register a callback for specific button(s). Returns an ID for removal.
    // Callbacks are only invoked when in edit mode.
    CallbackId AddVrButtonCallback(uint64_t buttonMask, VrButtonCallback callback);

    // Remove a callback by its ID
    void RemoveVrButtonCallback(CallbackId id);

private:
    EditModeInputManager() = default;
    ~EditModeInputManager() = default;
    EditModeInputManager(const EditModeInputManager&) = delete;
    EditModeInputManager& operator=(const EditModeInputManager&) = delete;

    // Internal callback registered with InputManager
    bool OnButtonEvent(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    struct ButtonCallbackEntry {
        CallbackId id;
        uint64_t buttonMask;
        VrButtonCallback callback;
    };

    bool m_initialized = false;
    std::vector<ButtonCallbackEntry> m_callbacks;
    CallbackId m_nextCallbackId = 1;  // 0 is InvalidCallbackId

    // ID of our callback registered with the main InputManager
    InputManager::CallbackId m_inputManagerCallbackId = InputManager::InvalidCallbackId;
};
