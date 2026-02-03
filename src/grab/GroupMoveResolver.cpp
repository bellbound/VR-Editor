#include "GroupMoveResolver.h"
#include "RemoteNPCPlacementManager.h"
#include "RemoteGrabController.h"
#include "../util/TouchingObjectsFinder.h"
#include "../log.h"

namespace Grab {

bool GroupMoveResolver::IsNPC(RE::TESObjectREFR* ref)
{
    return RemoteNPCPlacementManager::IsNPC(ref);
}

bool GroupMoveResolver::IsClutterOrPhysicsObject(RE::TESObjectREFR* ref)
{
    return RemoteGrabController::IsClutterOrPhysicsObject(ref);
}

GroupMoveResolver::SkipReason GroupMoveResolver::GetSkipReason(
    const std::vector<Selection::SelectionInfo>& selections,
    bool groupMoveEnabled)
{
    if (!groupMoveEnabled) {
        return SkipReason::Disabled;
    }

    if (selections.empty()) {
        return SkipReason::None;  // Nothing to skip, but also nothing to do
    }

    // Check for NPC-only selection (single NPC)
    if (selections.size() == 1 && IsNPC(selections[0].ref)) {
        return SkipReason::NPCOnlySelection;
    }

    // Check if primary selection is clutter/physics object
    if (selections[0].ref && IsClutterOrPhysicsObject(selections[0].ref)) {
        return SkipReason::PrimaryIsClutter;
    }

    return SkipReason::None;
}

bool GroupMoveResolver::ShouldSkipGroupMove(
    const std::vector<Selection::SelectionInfo>& selections,
    bool groupMoveEnabled)
{
    return GetSkipReason(selections, groupMoveEnabled) != SkipReason::None;
}

size_t GroupMoveResolver::AutoIncludeTouchingObjects(
    const std::vector<Selection::SelectionInfo>& selections,
    bool groupMoveEnabled,
    const Config& config)
{
    SkipReason reason = GetSkipReason(selections, groupMoveEnabled);

    if (reason != SkipReason::None) {
        switch (reason) {
            case SkipReason::Disabled:
                spdlog::trace("GroupMoveResolver: Group move disabled, skipping");
                break;
            case SkipReason::NPCOnlySelection:
                spdlog::trace("GroupMoveResolver: NPC-only selection, skipping");
                break;
            case SkipReason::PrimaryIsClutter:
                spdlog::trace("GroupMoveResolver: Primary selection is clutter, skipping");
                break;
            default:
                break;
        }
        return 0;
    }

    // Build list of current selection refs (excluding NPCs)
    std::vector<RE::TESObjectREFR*> currentRefs;
    currentRefs.reserve(selections.size());

    for (const auto& sel : selections) {
        if (sel.ref && !IsNPC(sel.ref)) {
            currentRefs.push_back(sel.ref);
        }
    }

    if (currentRefs.empty()) {
        return 0;
    }

    // Configure the touching objects finder
    Util::TouchingObjectsFinder::Config finderConfig;
    finderConfig.aabbExpansion = config.aabbExpansion;
    finderConfig.maxSearchRadius = config.maxSearchRadius;
    finderConfig.clutterOnly = config.clutterOnly;
    finderConfig.includeProps = config.includeProps;
    finderConfig.maxTouchingObjects = config.maxTouchingObjects;

    // Find and add touching objects
    size_t added = Util::TouchingObjectsFinder::AddTouchingObjectsToSelection(currentRefs, finderConfig);

    if (added > 0) {
        spdlog::info("GroupMoveResolver: Auto-included {} touching objects", added);
    }

    return added;
}

} // namespace Grab
