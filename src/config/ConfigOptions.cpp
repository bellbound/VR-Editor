#include "ConfigOptions.h"
#include "ConfigStorage.h"
#include "../log.h"

namespace Config {

void RegisterConfigOptions()
{
    auto* config = ConfigStorage::GetSingleton();

    if (!config->IsInitialized()) {
        spdlog::error("ConfigOptions: Cannot register options - ConfigStorage not initialized!");
        return;
    }

    spdlog::info("ConfigOptions: Registering default values for all options...");

    // =========================================================================
    // [General] Section - Core mod state
    // =========================================================================

    // Shows whether Edit mode is enabled or disabled.
    // Can be checked to enter / exit edit mode.
    // Default: false (0) - edit mode starts disabled
    config->RegisterIntOption(Options::kEditModeEnabled, 0);

    // Tracks whether the tutorial has been shown to the user.
    // The Tutorial needs to only be shown once.
    // Default: false (0) - tutorial not yet shown
    config->RegisterIntOption(Options::kTutorialShown, 0);

    // =========================================================================
    // [Controls] Section - Input and interaction settings
    // =========================================================================

    // Adds / Removes the Toggle Edit Mode spell from your Spell List.
    // When enabled, player receives a spell that can toggle edit mode.
    // Default: false (0) - spell not added by default
    config->RegisterIntOption(Options::kToggleSpellEnabled, 0);

    // Put your hand into a static object and double tap trigger to quickly toggle edit mode.
    // You can also set up a VRIK Gesture in the VRIK MCM instead.
    // Default: true (1) - quick edit enabled by default for convenience
    config->RegisterIntOption(Options::kQuickEditEnabled, 1);

    // Selecting an object in VR Editor also selects it in the console.
    // This allows immediate use of console commands on the selected object.
    // Default: true (1) - enabled by default for power users
    config->RegisterIntOption(Options::kSelectInConsole, 0);

    // =========================================================================
    // [Grid] Section - Snap-to-grid settings
    // =========================================================================

    // Position snap grid size in game units (1 meter ~ 70 units).
    // Default: 20.0
    config->RegisterFloatOption(Options::kPositionGridSize, 20.0f);

    // Whether Snap to Grid also snaps rotation to fixed stops.
    // Default: true (1)
    config->RegisterIntOption(Options::kRotationSnappingEnabled, 1);

    // Number of rotation snap stops around a full circle (e.g., 24 = 15°).
    // Default: 24
    config->RegisterIntOption(Options::kRotationSnappingStops, 24);

    // =========================================================================
    // [Persistence] Section - File saving options
    // =========================================================================

    // When enabled, creates separate INI files per cell.
    // When disabled, creates single consolidated files.
    // Default: false (0) - single file mode is cleaner for users
    config->RegisterIntOption(Options::kSavePerCell, 0);

    spdlog::info("ConfigOptions: Registered {} options", 9);
}

} // namespace Config
