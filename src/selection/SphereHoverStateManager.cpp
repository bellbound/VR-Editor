#include "SphereHoverStateManager.h"
#include "SelectionState.h"
#include "../visuals/ObjectHighlighter.h"
#include "../log.h"

namespace Selection {

SphereHoverStateManager* SphereHoverStateManager::GetSingleton()
{
    static SphereHoverStateManager instance;
    return &instance;
}

void SphereHoverStateManager::SetHoveredObjects(const std::vector<RE::TESObjectREFR*>& objects)
{
    // Build set of incoming FormIDs for fast lookup
    std::unordered_set<RE::FormID> incomingFormIds;
    for (auto* ref : objects) {
        if (ref) {
            incomingFormIds.insert(ref->GetFormID());
        }
    }

    // Find objects that are no longer hovered (were in m_hoveredFormIds but not in incoming)
    std::vector<RE::FormID> toRemove;
    for (RE::FormID formId : m_hoveredFormIds) {
        if (incomingFormIds.find(formId) == incomingFormIds.end()) {
            toRemove.push_back(formId);
        }
    }

    // Remove highlights from objects that left the sphere
    for (RE::FormID formId : toRemove) {
        // Look up the reference by FormID
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId);
        if (ref) {
            RemoveHoverHighlight(ref);
        } else {
            // Reference no longer valid, just unhighlight by FormID
            ObjectHighlighter::UnhighlightByFormId(formId);
        }
        m_hoveredFormIds.erase(formId);
    }

    // Find and highlight new objects (in incoming but not in m_hoveredFormIds)
    for (auto* ref : objects) {
        if (!ref) continue;

        RE::FormID formId = ref->GetFormID();
        if (m_hoveredFormIds.find(formId) == m_hoveredFormIds.end()) {
            // New object entering the sphere
            m_hoveredFormIds.insert(formId);
            ApplyHoverHighlight(ref);
        }
    }

    // Update the reference vector
    m_hoveredRefs.reserve(objects.size());
    m_hoveredRefs.clear();
    for (auto* ref : objects) {
        if (ref) {
            m_hoveredRefs.push_back(ref);
        }
    }

    // Log if there's a significant change
    if (!toRemove.empty() || m_hoveredRefs.size() != m_hoveredFormIds.size()) {
        spdlog::trace("SphereHoverStateManager: Now hovering {} objects", m_hoveredRefs.size());
    }
}

bool SphereHoverStateManager::IsHovered(RE::TESObjectREFR* ref) const
{
    if (!ref) return false;
    return m_hoveredFormIds.find(ref->GetFormID()) != m_hoveredFormIds.end();
}

bool SphereHoverStateManager::IsHovered(RE::FormID formId) const
{
    return m_hoveredFormIds.find(formId) != m_hoveredFormIds.end();
}

void SphereHoverStateManager::Clear()
{
    // Remove highlights from all hovered objects
    for (auto* ref : m_hoveredRefs) {
        if (ref) {
            RemoveHoverHighlight(ref);
        }
    }

    // Also unhighlight by FormID in case references became invalid
    for (RE::FormID formId : m_hoveredFormIds) {
        ObjectHighlighter::UnhighlightByFormId(formId);
    }

    m_hoveredRefs.clear();
    m_hoveredFormIds.clear();

}

void SphereHoverStateManager::ApplyHoverHighlight(RE::TESObjectREFR* ref)
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

void SphereHoverStateManager::RemoveHoverHighlight(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }

    // Don't remove highlight if object is selected (it should keep selection highlight)
    if (SelectionState::GetSingleton()->IsSelected(ref)) {
        return;
    }

    ObjectHighlighter::Unhighlight(ref);
}

} // namespace Selection
