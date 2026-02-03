#include "EditModeInputManager.h"
#include "EditModeManager.h"
#include "log.h"
#include "util/MenuChecker.h"
#include <algorithm>

EditModeInputManager* EditModeInputManager::GetSingleton()
{
    static EditModeInputManager instance;
    return &instance;
}

void EditModeInputManager::Initialize()
{
    if (m_initialized) {
        spdlog::warn("EditModeInputManager already initialized");
        return;
    }

    auto* inputManager = InputManager::GetSingleton();
    if (!inputManager->IsInitialized()) {
        spdlog::error("EditModeInputManager: InputManager not initialized");
        return;
    }

    // Register a catch-all callback with InputManager that filters by edit mode
    // We use 0xFFFFFFFFFFFFFFFF to match all buttons
    m_inputManagerCallbackId = inputManager->AddVrButtonCallback(
        0xFFFFFFFFFFFFFFFFULL,
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnButtonEvent(isLeft, isReleased, buttonId);
        }
    );

    if (m_inputManagerCallbackId == InputManager::InvalidCallbackId) {
        spdlog::error("EditModeInputManager: Failed to register with InputManager");
        return;
    }

    m_initialized = true;
    spdlog::info("EditModeInputManager initialized");
}

void EditModeInputManager::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    if (m_inputManagerCallbackId != InputManager::InvalidCallbackId) {
        InputManager::GetSingleton()->RemoveVrButtonCallback(m_inputManagerCallbackId);
        m_inputManagerCallbackId = InputManager::InvalidCallbackId;
    }

    m_callbacks.clear();
    m_initialized = false;
    spdlog::info("EditModeInputManager shut down");
}

EditModeInputManager::CallbackId EditModeInputManager::AddVrButtonCallback(uint64_t buttonMask, VrButtonCallback callback)
{
    CallbackId id = m_nextCallbackId++;
    m_callbacks.push_back({id, buttonMask, std::move(callback)});
    spdlog::info("EditModeInputManager: Added callback {} for mask 0x{:X}", id, buttonMask);
    return id;
}

void EditModeInputManager::RemoveVrButtonCallback(CallbackId id)
{
    if (id == InvalidCallbackId) {
        return;
    }

    auto it = std::find_if(m_callbacks.begin(), m_callbacks.end(),
        [id](const ButtonCallbackEntry& entry) { return entry.id == id; });

    if (it != m_callbacks.end()) {
        spdlog::info("EditModeInputManager: Removed callback {} for mask 0x{:X}", id, it->buttonMask);
        m_callbacks.erase(it);
    }
}

bool EditModeInputManager::OnButtonEvent(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only dispatch events when in edit mode
    if (!IsInEditMode()) {
        return false;  // Don't consume, let other handlers process
    }

    // Don't dispatch while a blocking menu is open
    if (MenuChecker::IsGameStopped()) {
        return false;
    }

    // Note: 3DUI hover check is handled at InputManager level - trigger/grip are filtered there

    uint64_t buttonMask = 1ULL << buttonId;
    bool consumed = false;

    // Make a copy of callbacks before iterating (callbacks may modify the list)
    auto callbacksCopy = m_callbacks;

    for (const auto& entry : callbacksCopy) {
        if (entry.buttonMask & buttonMask) {
            if (entry.callback(isLeft, isReleased, buttonId)) {
                consumed = true;
            }
        }
    }

    return consumed;
}
