#pragma once

// Manages the edit mode state for the in-game patcher
// When in edit mode, normal gameplay is paused and we take over input handling
class EditModeManager
{
public:
    static EditModeManager* GetSingleton();

    // Call once at startup after HIGGS interface is available
    // Reads and stores original HIGGS settings
    void Initialize();

    // Enter edit mode - disables HIGGS grip/trigger and SkyrimNet VR input
    void Enter();

    // Exit edit mode - restores HIGGS grip/trigger and SkyrimNet settings
    void Exit();

    // Check if currently in edit mode
    bool IsInEditMode() const { return m_isInEditMode; }

    bool IsInitialized() const { return m_initialized; }

private:
    EditModeManager() = default;
    ~EditModeManager() = default;
    EditModeManager(const EditModeManager&) = delete;
    EditModeManager& operator=(const EditModeManager&) = delete;

    // Disable HIGGS grip and trigger
    void DisableHiggs();

    // Restore HIGGS grip and trigger to original values
    void RestoreHiggs();

    // Enable SkyrimNet C++ hotkeys (blocks VR input)
    void EnableSkyrimNetHotkeys();

    // Restore SkyrimNet hotkeys to original state
    void RestoreSkyrimNetHotkeys();

    bool m_initialized = false;
    bool m_isInEditMode = false;

    // Original HIGGS settings (stored at initialization)
    double m_originalEnableTrigger = 1.0;  // 1 = enabled, 0 = disabled
    double m_originalEnableGrip = 1.0;     // 1 = enabled, 0 = disabled

    // Track if we modified each setting (to avoid restoring settings we didn't change)
    bool m_triggerDisabledByUs = false;
    bool m_gripDisabledByUs = false;

    // SkyrimNet hotkey state tracking
    // We assume hotkeys are disabled by default (VR input allowed)
    bool m_originalSkyrimNetHotkeysEnabled = false;
    bool m_skyrimNetHotkeysChangedByUs = false;
};

// Global convenience accessor
inline bool IsInEditMode() {
    return EditModeManager::GetSingleton()->IsInEditMode();
}
