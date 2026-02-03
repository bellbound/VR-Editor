#pragma once

#include "../selection/SelectionState.h"
#include <RE/Skyrim.h>
#include <vector>

namespace Grab {

// GroupMoveResolver: Handles automatic inclusion of touching objects when grabbing.
//
// When "Group Move" mode is enabled and the user grabs an object like a table,
// this class detects objects sitting on or touching that table (plates, cups, etc.)
// and automatically adds them to the selection so they move together.
//
// Conditions that skip group move:
// 1. Group move mode is disabled (toggled off via UI)
// 2. NPC-only selection (NPCs never group-move other objects)
// 3. Primary selection is itself a clutter/physics object (e.g., grabbing a plate off a table)
//
class GroupMoveResolver
{
public:
    struct Config {
        float aabbExpansion = 5.0f;      // Expand AABB by this amount for "touching" detection
        float maxSearchRadius = 300.0f;  // Search within this radius of selection
        bool clutterOnly = true;         // Only include clutter-type objects
        bool includeProps = true;        // Also include props
        size_t maxTouchingObjects = 20;  // Cap on additional objects
    };

    // Automatically include touching objects in the selection if conditions are met.
    // Returns the number of objects added to the selection.
    //
    // Parameters:
    //   selections: Current selection entries from SelectionState
    //   groupMoveEnabled: Whether group move mode is enabled (from UI toggle)
    //   config: Configuration for touching object detection
    //
    // Side effect: Adds objects to SelectionState if any are found
    static size_t AutoIncludeTouchingObjects(
        const std::vector<Selection::SelectionInfo>& selections,
        bool groupMoveEnabled,
        const Config& config = Config{});

    // Check if group move should be skipped for this selection
    // Returns: true if group move should be skipped, false if it should proceed
    static bool ShouldSkipGroupMove(
        const std::vector<Selection::SelectionInfo>& selections,
        bool groupMoveEnabled);

    // Reason why group move was skipped (for logging)
    enum class SkipReason {
        None,               // Group move was not skipped
        Disabled,           // Group move mode is disabled
        NPCOnlySelection,   // Selection contains only a single NPC
        PrimaryIsClutter    // Primary selected object is clutter/physics object
    };

    // Get the reason why group move would be skipped
    static SkipReason GetSkipReason(
        const std::vector<Selection::SelectionInfo>& selections,
        bool groupMoveEnabled);

private:
    // Check if a reference is an NPC/Actor
    static bool IsNPC(RE::TESObjectREFR* ref);

    // Check if a reference is a clutter/physics object (plates, cups, etc.)
    static bool IsClutterOrPhysicsObject(RE::TESObjectREFR* ref);
};

} // namespace Grab
