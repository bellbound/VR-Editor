#pragma once

#include <RE/Skyrim.h>
#include <vector>
#include <unordered_set>

namespace Selection {

// SphereHoverStateManager: Tracks multiple hovered objects within a sphere volume
//
// Unlike HoverStateManager which tracks a single hovered object via ray casting,
// this manager tracks ALL objects within a selection sphere. It's designed for
// volume-based multi-selection where users position a sphere and select everything inside.
//
// Key differences from HoverStateManager:
// - Tracks multiple objects simultaneously
// - No debounce needed (sphere position is stable, not a ray)
// - Provides diff-based updates (only changes highlights when set changes)
//
// Usage:
// - SphereSelectionController calls SetHoveredObjects() with all objects in sphere
// - This manager diffs against previous state and updates highlights accordingly
// - EditModeStateManager calls GetHoveredObjects() on trigger to get selection targets
//
class SphereHoverStateManager
{
public:
    static SphereHoverStateManager* GetSingleton();

    // Set all objects currently within the selection sphere
    // This automatically diffs against the previous set and updates highlights:
    // - Objects no longer present get unhighlighted
    // - New objects get highlighted (unless already selected)
    void SetHoveredObjects(const std::vector<RE::TESObjectREFR*>& objects);

    // Get all currently hovered objects
    const std::vector<RE::TESObjectREFR*>& GetHoveredObjects() const { return m_hoveredRefs; }

    // Check if any objects are currently hovered
    bool HasHoveredObjects() const { return !m_hoveredRefs.empty(); }

    // Get count of hovered objects
    size_t GetHoveredCount() const { return m_hoveredRefs.size(); }

    // Check if a specific object is in the hover set
    bool IsHovered(RE::TESObjectREFR* ref) const;
    bool IsHovered(RE::FormID formId) const;

    // Clear all hover state and remove highlights
    void Clear();

private:
    SphereHoverStateManager() = default;
    ~SphereHoverStateManager() = default;
    SphereHoverStateManager(const SphereHoverStateManager&) = delete;
    SphereHoverStateManager& operator=(const SphereHoverStateManager&) = delete;

    // Apply hover highlight to an object (if not already selected)
    void ApplyHoverHighlight(RE::TESObjectREFR* ref);

    // Remove hover highlight from an object (if not selected)
    void RemoveHoverHighlight(RE::TESObjectREFR* ref);

    // Currently hovered objects
    std::vector<RE::TESObjectREFR*> m_hoveredRefs;

    // Fast lookup set for diffing (stores FormIDs for stability)
    std::unordered_set<RE::FormID> m_hoveredFormIds;
};

} // namespace Selection
