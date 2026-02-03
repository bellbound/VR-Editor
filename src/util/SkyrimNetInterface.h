#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <optional>

// Interface to SkyrimNet's Papyrus API for controlling VR input blocking
// SkyrimNet's C++ hotkey manager intercepts VR inputs when enabled
class SkyrimNetInterface
{
public:
    static SkyrimNetInterface* GetSingleton();

    // Initialize - checks if SkyrimNet is available and caches function availability
    void Initialize();

    // Check if SkyrimNet is available
    bool IsAvailable() const { return m_available; }

    // Check if the hotkey control functions are available
    bool HasHotkeyFunctions() const { return m_hasHotkeyFunctions; }

    // Enable or disable the C++ hotkey manager (disabling VR input when enabled)
    // Returns 0 on success, 1 on failure
    // When enabled=true, VR input is blocked by SkyrimNet's hotkey manager
    int SetCppHotkeysEnabled(bool enabled);

    // Check if C++ hotkey manager is currently enabled
    // Returns std::nullopt if function doesn't exist or call failed
    std::optional<bool> IsCppHotkeysEnabled();

private:
    SkyrimNetInterface() = default;
    ~SkyrimNetInterface() = default;
    SkyrimNetInterface(const SkyrimNetInterface&) = delete;
    SkyrimNetInterface& operator=(const SkyrimNetInterface&) = delete;

    bool CheckAvailability();
    bool CheckHotkeyFunctions();

    RE::BSScript::Internal::VirtualMachine* GetVM();

    bool m_initialized = false;
    bool m_available = false;
    bool m_hasHotkeyFunctions = false;

    static constexpr const char* API_SCRIPT = "SkyrimNetApi";
};
