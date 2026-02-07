#pragma once

#include <memory>

namespace Persistence {
    class ChangedObjectRegistry;
}

/**
 * @brief Centralized system initialization controller for VR Editor
 *
 * SystemInitializer implements a two-phase initialization strategy to minimize
 * crash risk and startup overhead when the mod is installed but not actively used:
 *
 * ## Phase 1: PostLoadGame/NewGame Initialization
 * Called automatically when the player loads a save or starts a new game.
 * Initializes only the essential systems required for edit mode detection:
 * - InputManager: VR controller input hooks
 * - EditModeInputManager: Edit mode input layer
 * - EditModeTransitioner: Detects double-tap gesture to enter edit mode
 *
 * ## Phase 2: OnFirstUse Initialization
 * Called lazily when the player first enters edit mode (via MayInitializeOnFirstUse).
 * Initializes all remaining systems only when actually needed:
 * - Selection and grab controllers
 * - UI menus (SelectionMenu, GalleryMenu)
 * - Undo/Redo system
 *
 * This separation ensures that players who have the mod installed but don't use
 * edit mode won't experience any additional crash risk or performance impact.
 */
class SystemInitializer
{
public:
    static SystemInitializer* GetSingleton();

    /**
     * @brief Initialize core services required from plugin load
     *
     * Call this from SKSEPluginLoad, before registering serialization callbacks.
     * Creates core services that must exist before game events fire.
     * Safe to call multiple times; subsequent calls are no-ops.
     *
     * Creates:
     * - ChangedObjectRegistry (object change tracking)
     */
    void InitializeCoreSystems();

    /**
     * @brief Check if core systems are initialized
     */
    bool IsCoreInitialized() const { return m_CoreInitialized; }

    /**
     * @brief Get the ChangedObjectRegistry instance
     * @note Must call InitializeCoreSystems() first
     */
    Persistence::ChangedObjectRegistry& GetChangedObjectRegistry();

    /**
     * @brief Initialize essential systems required for edit mode detection
     *
     * Call this from both PostLoadGame and NewGame events in plugin.cpp.
     * Safe to call multiple times; subsequent calls are no-ops.
     *
     * Initializes:
     * - InputManager (VR input hooks)
     * - EditModeInputManager (edit mode input layer)
     * - EditModeTransitioner (double-tap detection)
     */
    void InitializeSystems_PostLoadGameOrNewGame();

    /**
     * @brief Conditionally initialize on-demand systems
     *
     * Call this from EditModeTransitioner when entering edit mode.
     * Safe to call every time; only performs initialization once.
     *
     * @return true if initialization was performed, false if already initialized
     */
    bool MayInitializeOnFirstUse();

    // Query initialization state
    bool IsPostLoadGameInitialized() const { return m_PostLoadGameInitialized; }
    bool IsFirstUseInitialized() const { return m_FirstUseInitialized; }

private:
    SystemInitializer() = default;
    ~SystemInitializer() = default;
    SystemInitializer(const SystemInitializer&) = delete;
    SystemInitializer& operator=(const SystemInitializer&) = delete;

    /**
     * @brief Initialize on-demand systems (internal implementation)
     *
     * Called by MayInitializeOnFirstUse when first entering edit mode.
     * Initializes all systems not required for edit mode detection.
     */
    void InitializeSystems_OnFirstUse();

    // Core services - available from plugin load
    std::unique_ptr<Persistence::ChangedObjectRegistry> m_changedObjectRegistry;

    bool m_CoreInitialized = false;
    bool m_PostLoadGameInitialized = false;
    bool m_FirstUseInitialized = false;
};
