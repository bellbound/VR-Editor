#include "SelectionState.h"
#include "../visuals/ObjectHighlighter.h"
#include "../actions/ActionHistoryRepository.h"
#include "../ui/SelectionMenu.h"
#include "../log.h"
#include <algorithm>

namespace Selection {

SelectionState* SelectionState::GetSingleton()
{
    static SelectionState instance;
    return &instance;
}

void SelectionState::Initialize()
{
    if (m_initialized) {
        spdlog::warn("SelectionState already initialized");
        return;
    }

    // Set up callback to record selection changes to action history and update UI
    m_changeCallback = [](const std::vector<SelectionInfo>& oldSelection,
                          const std::vector<SelectionInfo>& newSelection) {
        // Convert SelectionInfo vectors to FormID vectors
        std::vector<RE::FormID> oldFormIds;
        std::vector<RE::FormID> newFormIds;

        for (const auto& info : oldSelection) {
            oldFormIds.push_back(info.formId);
        }
        for (const auto& info : newSelection) {
            newFormIds.push_back(info.formId);
        }

        // Only record selection changes when going from multi-select (>1) to single/none (<=1)
        // Single selections and additions to selection are not user-visible undo actions
        if (oldFormIds != newFormIds && oldFormIds.size() > 1 && newFormIds.size() <= 1) {
            Actions::ActionHistoryRepository::GetSingleton()->AddSelection(oldFormIds, newFormIds);
        }

        // Update selection menu visibility (shows correct wheel for single/multi selection)
        SelectionMenu::GetSingleton()->UpdateSelectionMenuVisibility();
    };

    m_initialized = true;
    spdlog::info("SelectionState initialized");
}

void SelectionState::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    ClearAll();
    m_changeCallback = nullptr;

    m_initialized = false;
    spdlog::info("SelectionState shutdown");
}

SelectionInfo SelectionState::CreateSelectionInfo(RE::TESObjectREFR* ref)
{
    SelectionInfo info;
    if (!ref) {
        return info;
    }

    info.ref = ref;
    info.formId = ref->GetFormID();

    if (auto* node = ref->Get3D()) {
        info.transformAtSelection = node->world;
    } else {
        info.transformAtSelection.translate = ref->GetPosition();
        info.transformAtSelection.scale = 1.0f;
    }

    return info;
}

void SelectionState::ApplyHighlight(RE::TESObjectREFR* ref)
{
    if (!ref) return;

    // Effect shader approach: Highlight() automatically handles replacing any existing highlight
    ObjectHighlighter::Highlight(ref, ObjectHighlighter::HighlightType::Selection);
}

void SelectionState::RemoveHighlight(RE::TESObjectREFR* ref)
{
    if (!ref) return;

    // Effect shader approach uses FormID-based tracking, handles node pointer changes automatically
    ObjectHighlighter::Unhighlight(ref);
}

void SelectionState::SetSingleSelection(RE::TESObjectREFR* ref)
{
    std::vector<SelectionInfo> oldSelection = m_selection;

    // Toggle behavior: if clicking the only selected object, deselect it
    if (ref && m_selection.size() == 1 && IsSelected(ref)) {
        RemoveHighlight(ref);
        m_selection.clear();
        spdlog::info("SelectionState: Toggled off single selection {:08X}", ref->GetFormID());
        NotifySelectionChange(oldSelection);
        return;
    }

    // Remove highlights from all currently selected
    for (const auto& info : m_selection) {
        RemoveHighlight(info.ref);
    }
    m_selection.clear();

    // Add the new single selection
    if (ref) {
        SelectionInfo info = CreateSelectionInfo(ref);
        m_selection.push_back(info);
        ApplyHighlight(ref);
        spdlog::info("SelectionState: Set single selection to {:08X}", ref->GetFormID());
    }

    NotifySelectionChange(oldSelection);
}

void SelectionState::AddToSelection(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }

    // Check if already selected
    if (IsSelected(ref)) {
        spdlog::trace("SelectionState: {:08X} already in selection", ref->GetFormID());
        return;
    }

    std::vector<SelectionInfo> oldSelection = m_selection;

    SelectionInfo info = CreateSelectionInfo(ref);
    m_selection.push_back(info);
    ApplyHighlight(ref);

    spdlog::info("SelectionState: Added {:08X} to selection (count: {})",
        ref->GetFormID(), m_selection.size());

    NotifySelectionChange(oldSelection);
}

void SelectionState::RemoveFromSelection(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }

    auto it = std::find_if(m_selection.begin(), m_selection.end(),
        [ref](const SelectionInfo& info) { return info.formId == ref->GetFormID(); });

    if (it == m_selection.end()) {
        return;
    }

    std::vector<SelectionInfo> oldSelection = m_selection;

    RemoveHighlight(ref);
    m_selection.erase(it);

    spdlog::info("SelectionState: Removed {:08X} from selection (count: {})",
        ref->GetFormID(), m_selection.size());

    NotifySelectionChange(oldSelection);
}

void SelectionState::ToggleSelection(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }

    if (IsSelected(ref)) {
        RemoveFromSelection(ref);
    } else {
        AddToSelection(ref);
    }
}

bool SelectionState::IsSelected(RE::TESObjectREFR* ref) const
{
    if (!ref) {
        return false;
    }
    return IsSelected(ref->GetFormID());
}

bool SelectionState::IsSelected(RE::FormID formId) const
{
    return std::any_of(m_selection.begin(), m_selection.end(),
        [formId](const SelectionInfo& info) { return info.formId == formId; });
}

RE::TESObjectREFR* SelectionState::GetFirstSelected() const
{
    if (m_selection.empty()) {
        return nullptr;
    }
    return m_selection[0].ref;
}

const SelectionInfo* SelectionState::GetFirstSelectionInfo() const
{
    if (m_selection.empty()) {
        return nullptr;
    }
    return &m_selection[0];
}

void SelectionState::ClearAll()
{
    if (m_selection.empty()) {
        return;
    }

    std::vector<SelectionInfo> oldSelection = m_selection;

    // Remove all highlights
    for (const auto& info : m_selection) {
        RemoveHighlight(info.ref);
    }
    m_selection.clear();

    spdlog::info("SelectionState: Cleared all selections");

    NotifySelectionChange(oldSelection);
}

void SelectionState::ReduceToSingle()
{
    if (m_selection.size() <= 1) {
        return;  // Already single or empty
    }

    std::vector<SelectionInfo> oldSelection = m_selection;

    // Keep only the first item, remove highlights from the rest
    for (size_t i = 1; i < m_selection.size(); ++i) {
        RemoveHighlight(m_selection[i].ref);
    }

    SelectionInfo first = m_selection[0];
    m_selection.clear();
    m_selection.push_back(first);

    spdlog::info("SelectionState: Reduced to single selection {:08X}", first.formId);

    NotifySelectionChange(oldSelection);
}

void SelectionState::NotifySelectionChange(const std::vector<SelectionInfo>& oldSelection)
{
    // Skip callback during undo/redo to avoid recording changes we're restoring
    if (m_suppressCallback) {
        return;
    }

    if (m_changeCallback) {
        m_changeCallback(oldSelection, m_selection);
    }
}

void SelectionState::RefreshHighlightIfSelected(RE::TESObjectREFR* ref)
{
    if (!ref) return;

    if (IsSelected(ref)) {
        ApplyHighlight(ref);
        spdlog::trace("SelectionState: Refreshed highlight on {:08X}", ref->GetFormID());
    }
}

} // namespace Selection
