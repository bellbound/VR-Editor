#pragma once

#include <string_view>

namespace Config {

/// String constants for all configuration option keys.
/// Use these instead of hardcoding strings to prevent typos and enable refactoring.
///
/// Naming convention:
/// - k prefix for constants
/// - Section prefix matches INI section (General, Controls, Persistence, Grid)
/// - Descriptive name in PascalCase
///
/// Key format in INI: "Section:KeyName" -> [Section] KeyName=value
namespace Options {

    // =========================================================================
    // [General] Section - Core mod state
    // =========================================================================

    /// Shows whether Edit mode is enabled or disabled.
    /// Can be checked to enter / exit edit mode.
    /// Type: bool (stored as int 0/1)
    /// Default: false (0)
    constexpr std::string_view kEditModeEnabled = "General:bEditModeEnabled";

    /// Tracks whether the tutorial has been shown to the user.
    /// The Tutorial needs to only be shown once.
    /// NOTE: This option has no MCM counterpart - it is internal tracking only.
    /// Type: bool (stored as int 0/1)
    /// Default: false (0) - tutorial not yet shown
    constexpr std::string_view kTutorialShown = "General:bTutorialShown";

    // =========================================================================
    // [Controls] Section - Input and interaction settings
    // =========================================================================

    /// Adds / Removes the Toggle Edit Mode spell from your Spell List.
    /// When enabled, player receives a spell that can toggle edit mode.
    /// Type: bool (stored as int 0/1)
    /// Default: false (0)
    constexpr std::string_view kToggleSpellEnabled = "Controls:bToggleSpellEnabled";

    /// Put your hand into a static object and double tap trigger to quickly toggle edit mode.
    /// You can also set up a VRIK Gesture in the VRIK MCM instead.
    /// Type: bool (stored as int 0/1)
    /// Default: true (1)
    constexpr std::string_view kQuickEditEnabled = "Controls:bQuickEditEnabled";

    /// Selecting an object in VR Editor also selects it in the console.
    /// This allows immediate use of console commands on the selected object.
    /// Type: bool (stored as int 0/1)
    /// Default: true (1)
    constexpr std::string_view kSelectInConsole = "Controls:bSelectInConsole";

    // =========================================================================
    // [Grid] Section - Snap-to-grid settings
    // =========================================================================

    /// Position snap grid size in game units (1 meter ~ 70 units).
    /// Type: float
    /// Default: 20.0
    constexpr std::string_view kPositionGridSize = "Grid:fPositionGridSize";

    /// Whether Snap to Grid also snaps rotation to fixed stops.
    /// Type: bool (stored as int 0/1)
    /// Default: true (1)
    constexpr std::string_view kRotationSnappingEnabled = "Grid:bRotationSnappingEnabled";

    /// Number of rotation snap stops around a full circle (e.g., 24 = 15°).
    /// Type: int
    /// Default: 24
    constexpr std::string_view kRotationSnappingStops = "Grid:iRotationSnappingStops";

    // =========================================================================
    // [Persistence] Section - File saving options
    // =========================================================================

    /// When enabled, creates separate INI files per cell (current behavior).
    /// When disabled, creates single consolidated files:
    /// - Data/VREditor_SWAP.ini (for BOS transforms)
    /// - Data/VREditor_SWAP_latest.ini (session changes)
    /// - Data/SKSE/Plugins/VREditor/VREditor_AddedObjects.ini (spawned objects)
    /// Type: bool (stored as int 0/1)
    /// Default: false (0) - single file mode
    constexpr std::string_view kSavePerCell = "Persistence:bSavePerCell";

} // namespace Options

/// Initialize all config options with their default values.
/// Must be called after ConfigStorage::Initialize() but before any options are read.
/// This ensures all options exist in the INI with sensible defaults on first run.
void RegisterConfigOptions();

} // namespace Config
