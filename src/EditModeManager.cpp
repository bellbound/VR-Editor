#include "EditModeManager.h"
#include "EditModeStateManager.h"
#include "config/ConfigStorage.h"
#include "config/ConfigOptions.h"
#include "interfaces/higgsinterface001.h"
#include "util/SkyrimNetInterface.h"
#include <spdlog/spdlog.h>

EditModeManager* EditModeManager::GetSingleton()
{
    static EditModeManager instance;
    return &instance;
}

void EditModeManager::Initialize()
{
    if (m_initialized) {
        return;
    }

    // Reset edit mode state in config on game start
    // This ensures we always start with edit mode disabled
    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kEditModeEnabled, 0);

    if (!g_higgsInterface) {
        spdlog::warn("EditModeManager: HIGGS interface not available");
        return;
    }

    // Read and store the original HIGGS settings
    if (g_higgsInterface->GetSettingDouble("EnableTrigger", m_originalEnableTrigger)) {
        spdlog::info("EditModeManager: Original EnableTrigger = {}", m_originalEnableTrigger);
    } else {
        spdlog::warn("EditModeManager: Failed to read EnableTrigger, assuming 1 (enabled)");
        m_originalEnableTrigger = 1.0;
    }

    if (g_higgsInterface->GetSettingDouble("EnableGrip", m_originalEnableGrip)) {
        spdlog::info("EditModeManager: Original EnableGrip = {}", m_originalEnableGrip);
    } else {
        spdlog::warn("EditModeManager: Failed to read EnableGrip, assuming 1 (enabled)");
        m_originalEnableGrip = 1.0;
    }

    m_initialized = true;
    spdlog::info("EditModeManager: Initialized");
}

void EditModeManager::Enter()
{
    if (m_isInEditMode) {
        spdlog::debug("EditModeManager: Already in edit mode");
        return;
    }

    spdlog::info("EditModeManager: Entering edit mode");
    DisableHiggs();
    EnableSkyrimNetHotkeys();
    m_isInEditMode = true;

    // Sync config state for MCM display
    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kEditModeEnabled, 1);

    // Notify state manager to enter initial state (Remote Selection Mode)
    EditModeStateManager::GetSingleton()->OnEditModeEnter();
}

void EditModeManager::Exit()
{
    if (!m_isInEditMode) {
        spdlog::debug("EditModeManager: Not in edit mode");
        return;
    }

    spdlog::info("EditModeManager: Exiting edit mode");

    // Notify state manager to clean up before exiting
    EditModeStateManager::GetSingleton()->OnEditModeExit();

    RestoreHiggs();
    RestoreSkyrimNetHotkeys();
    m_isInEditMode = false;

    // Sync config state for MCM display
    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kEditModeEnabled, 0);
}

void EditModeManager::DisableHiggs()
{
    if (!m_initialized || !g_higgsInterface) {
        spdlog::warn("EditModeManager: Cannot disable HIGGS - not initialized");
        return;
    }

    // Disable trigger (if not already disabled by user)
    if (!m_triggerDisabledByUs && m_originalEnableTrigger != 0.0) {
        if (g_higgsInterface->SetSettingDouble("EnableTrigger", 0.0)) {
            m_triggerDisabledByUs = true;
            spdlog::debug("EditModeManager: Disabled HIGGS trigger");
        } else {
            spdlog::warn("EditModeManager: Failed to disable HIGGS trigger");
        }
    }

    // Disable grip (if not already disabled by user)
    if (!m_gripDisabledByUs && m_originalEnableGrip != 0.0) {
        if (g_higgsInterface->SetSettingDouble("EnableGrip", 0.0)) {
            m_gripDisabledByUs = true;
            spdlog::debug("EditModeManager: Disabled HIGGS grip");
        } else {
            spdlog::warn("EditModeManager: Failed to disable HIGGS grip");
        }
    }
}

void EditModeManager::RestoreHiggs()
{
    if (!g_higgsInterface) {
        spdlog::warn("EditModeManager: Cannot restore HIGGS - interface not available");
        return;
    }

    // Restore trigger (only if we disabled it)
    if (m_triggerDisabledByUs) {
        if (g_higgsInterface->SetSettingDouble("EnableTrigger", m_originalEnableTrigger)) {
            m_triggerDisabledByUs = false;
            spdlog::debug("EditModeManager: Restored HIGGS trigger to {}", m_originalEnableTrigger);
        } else {
            spdlog::warn("EditModeManager: Failed to restore HIGGS trigger");
        }
    }

    // Restore grip (only if we disabled it)
    if (m_gripDisabledByUs) {
        if (g_higgsInterface->SetSettingDouble("EnableGrip", m_originalEnableGrip)) {
            m_gripDisabledByUs = false;
            spdlog::debug("EditModeManager: Restored HIGGS grip to {}", m_originalEnableGrip);
        } else {
            spdlog::warn("EditModeManager: Failed to restore HIGGS grip");
        }
    }
}

void EditModeManager::EnableSkyrimNetHotkeys()
{
    auto* skyrimNet = SkyrimNetInterface::GetSingleton();

    // Only proceed if SkyrimNet is available and has the hotkey functions
    if (!skyrimNet->IsAvailable() || !skyrimNet->HasHotkeyFunctions()) {
        spdlog::debug("EditModeManager: SkyrimNet hotkey control not available");
        return;
    }

    // Enable C++ hotkeys (this blocks VR input in SkyrimNet)
    // We assume the original state is disabled (VR input allowed) since we can't
    // easily get the current state synchronously from Papyrus
    if (!m_skyrimNetHotkeysChangedByUs) {
        int result = skyrimNet->SetCppHotkeysEnabled(true);
        if (result == 0) {
            m_skyrimNetHotkeysChangedByUs = true;
            spdlog::info("EditModeManager: Enabled SkyrimNet C++ hotkeys (blocking VR input)");
        } else {
            spdlog::warn("EditModeManager: Failed to enable SkyrimNet C++ hotkeys");
        }
    }
}

void EditModeManager::RestoreSkyrimNetHotkeys()
{
    auto* skyrimNet = SkyrimNetInterface::GetSingleton();

    // Only restore if we changed it
    if (!m_skyrimNetHotkeysChangedByUs) {
        return;
    }

    if (!skyrimNet->IsAvailable() || !skyrimNet->HasHotkeyFunctions()) {
        spdlog::debug("EditModeManager: SkyrimNet hotkey control not available for restore");
        m_skyrimNetHotkeysChangedByUs = false;
        return;
    }

    // Restore to original state (disabled = VR input allowed)
    int result = skyrimNet->SetCppHotkeysEnabled(m_originalSkyrimNetHotkeysEnabled);
    if (result == 0) {
        m_skyrimNetHotkeysChangedByUs = false;
        spdlog::info("EditModeManager: Restored SkyrimNet C++ hotkeys to {}", m_originalSkyrimNetHotkeysEnabled);
    } else {
        spdlog::warn("EditModeManager: Failed to restore SkyrimNet C++ hotkeys");
    }
}
