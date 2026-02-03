#include "HoverStateManager.h"
#include "SelectionState.h"
#include "../visuals/ObjectHighlighter.h"

namespace Selection {

HoverStateManager* HoverStateManager::GetSingleton()
{
    static HoverStateManager instance;
    return &instance;
}

void HoverStateManager::SetPendingHover(RE::TESObjectREFR* ref)
{
    if (m_pendingRef == ref) {
        // Same object, debounce timer continues in Update()
        return;
    }

    // Different object - reset debounce timer
    m_pendingRef = ref;
    m_pendingTime = 0.0f;
}

void HoverStateManager::SetPendingHoverWithRetention(RE::TESObjectREFR* primaryHit, const std::vector<RE::TESObjectREFR*>& retentionHits)
{
    // Helper to check if an object is in the retention hits
    auto isInRetentionHits = [&retentionHits](RE::TESObjectREFR* ref) -> bool {
        if (!ref) return false;
        for (auto* hit : retentionHits) {
            if (hit == ref) return true;
        }
        return false;
    };

    // Sticky behavior: If current confirmed hover is still being hit by any ray, keep it
    if (m_hoveredRef && isInRetentionHits(m_hoveredRef)) {
        // Current highlight is retained - set pending to the same object
        if (m_pendingRef != m_hoveredRef) {
            m_pendingRef = m_hoveredRef;
            // Keep full debounce time since we're retaining, not acquiring
            m_pendingTime = kHoverDebounceTime;
        }
        return;
    }

    // Pending retention: If pending object (not yet confirmed) is still being hit, keep debouncing
    if (m_pendingRef && m_pendingRef != m_hoveredRef && isInRetentionHits(m_pendingRef)) {
        // Pending is still being hit, continue debounce (no change needed)
        return;
    }

    // Otherwise, use primary hit as the new target
    SetPendingHover(primaryHit);
}

void HoverStateManager::Update(float deltaTime)
{
    // If pending is null, clear confirmed hover
    if (!m_pendingRef) {
        if (m_hoveredRef) {
            RemoveHoverHighlight(m_hoveredRef);
            m_hoveredRef = nullptr;
        }
        return;
    }

    // If pending is already the confirmed hover, nothing to do
    if (m_pendingRef == m_hoveredRef) {
        return;
    }

    // Accumulate debounce time
    m_pendingTime += deltaTime;

    // Check if debounce threshold reached
    if (m_pendingTime >= kHoverDebounceTime) {
        // Remove highlight from old hover
        if (m_hoveredRef) {
            RemoveHoverHighlight(m_hoveredRef);
        }

        // Confirm the new hover
        m_hoveredRef = m_pendingRef;

        // Apply highlight to new hover
        ApplyHoverHighlight(m_hoveredRef);
    }
}

void HoverStateManager::Clear()
{
    if (m_hoveredRef) {
        RemoveHoverHighlight(m_hoveredRef);
        m_hoveredRef = nullptr;
    }
    m_pendingRef = nullptr;
    m_pendingTime = 0.0f;
}

void HoverStateManager::ApplyHoverHighlight(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }

    // Don't apply hover highlight if object is already selected (it has selection highlight)
    if (SelectionState::GetSingleton()->IsSelected(ref)) {
        return;
    }

    ObjectHighlighter::Highlight(ref, ObjectHighlighter::HighlightType::Hover);
}

void HoverStateManager::RemoveHoverHighlight(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }

    // Don't remove highlight if object is selected (it should keep selection highlight)
    if (SelectionState::GetSingleton()->IsSelected(ref)) {
        return;
    }

    // Effect shader approach uses FormID-based tracking, handles node pointer changes automatically
    ObjectHighlighter::Unhighlight(ref);
}

} // namespace Selection
