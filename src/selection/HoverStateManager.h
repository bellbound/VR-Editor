#pragma once

#include <RE/Skyrim.h>
#include <vector>

namespace Selection {

// HoverStateManager: Single source of truth for hover state during selection modes
//
// This singleton tracks which object is currently being hovered (pointed at)
// during QuickSelect and MultiSelect modes. It handles:
// - Tracking the currently hovered object reference with debounce
// - Applying/removing hover highlights
// - Providing a clean API for other systems to query hover state
//
// Debounce behavior:
// - Raw ray hits are passed to SetPendingHover() each frame
// - Only after the same object is hit for kHoverDebounceTime does it become the confirmed hover
// - This prevents flickering when the ray moves quickly between objects
//
// Usage:
// - RemoteSelectionController calls SetPendingHover() when ray casting finds an object
// - RemoteSelectionController calls Update() each frame to process debounce
// - EditModeStateManager calls GetHoveredObject() on trigger release to get the target
// - Clear() is called when exiting selection modes
//
class HoverStateManager
{
public:
    static HoverStateManager* GetSingleton();

    // Set the pending hover target (called by RemoteSelectionController during ray cast)
    // This starts/continues the debounce timer for this object
    void SetPendingHover(RE::TESObjectREFR* ref);

    // Set pending hover with retention support for multi-ray debouncing
    // primaryHit: The object hit by the central ray (used for acquiring new targets)
    // retentionHits: All objects hit by any of the 5 rays (used to retain current highlight)
    // If the currently highlighted object is in retentionHits, it stays highlighted (sticky behavior)
    void SetPendingHoverWithRetention(RE::TESObjectREFR* primaryHit, const std::vector<RE::TESObjectREFR*>& retentionHits);

    // Update debounce timer (call each frame)
    void Update(float deltaTime);

    // Get the confirmed hovered object (only valid after debounce completes)
    RE::TESObjectREFR* GetHoveredObject() const { return m_hoveredRef; }

    // Check if we have a confirmed hovered object
    bool HasHoveredObject() const { return m_hoveredRef != nullptr; }

    // Clear all hover state and remove highlight
    void Clear();

    // Hover highlight color (cyan)
    static constexpr RE::NiColor kHoverColor{0.2f, 0.8f, 1.0f};

    // Debounce time - object must be hovered this long before confirming
    // Reduced to 37.5ms since multi-ray implementation provides sufficient noise filtering
    static constexpr float kHoverDebounceTime = 0.0375f;  // 37.5ms (25% of original 150ms)

private:
    HoverStateManager() = default;
    ~HoverStateManager() = default;
    HoverStateManager(const HoverStateManager&) = delete;
    HoverStateManager& operator=(const HoverStateManager&) = delete;

    // Apply hover highlight to the object
    void ApplyHoverHighlight(RE::TESObjectREFR* ref);

    // Remove hover highlight from the object (if not selected)
    void RemoveHoverHighlight(RE::TESObjectREFR* ref);

    // Confirmed hover (after debounce)
    RE::TESObjectREFR* m_hoveredRef = nullptr;

    // Pending hover (before debounce completes)
    RE::TESObjectREFR* m_pendingRef = nullptr;
    float m_pendingTime = 0.0f;
};

} // namespace Selection
