#pragma once

#include <RE/Skyrim.h>
#include <vector>
#include <functional>

namespace Selection {

// Information about a selected object
struct SelectionInfo {
    RE::TESObjectREFR* ref = nullptr;
    RE::FormID formId = 0;
    RE::NiTransform transformAtSelection;  // Transform when selected (for undo)

    bool IsValid() const { return ref != nullptr && formId != 0; }

    bool operator==(const SelectionInfo& other) const {
        return formId == other.formId;
    }
};

// SelectionState: Manages the current selection of objects in edit mode
//
// Uses a single list for all selections:
// - SetSingleSelection() clears list and adds one item
// - AddToSelection() / ToggleSelection() adds/removes from list
// - Selection highlighting (different from hover highlighting)
//
class SelectionState
{
public:
    static SelectionState* GetSingleton();

    void Initialize();
    void Shutdown();

    // Single selection - clears existing and sets to one object
    void SetSingleSelection(RE::TESObjectREFR* ref);

    // Multi-selection operations
    void AddToSelection(RE::TESObjectREFR* ref);
    void RemoveFromSelection(RE::TESObjectREFR* ref);
    void ToggleSelection(RE::TESObjectREFR* ref);

    // Query selection
    const std::vector<SelectionInfo>& GetSelection() const { return m_selection; }
    bool HasSelection() const { return !m_selection.empty(); }
    bool HasAnySelection() const { return !m_selection.empty(); }  // Alias for compatibility
    bool IsSelected(RE::TESObjectREFR* ref) const;
    bool IsSelected(RE::FormID formId) const;
    size_t GetSelectionCount() const { return m_selection.size(); }

    // Get first selected (for single-object operations)
    RE::TESObjectREFR* GetFirstSelected() const;
    const SelectionInfo* GetFirstSelectionInfo() const;

    // Clear everything
    void ClearAll();

    // Reduce to single selection (keep only the first item)
    void ReduceToSingle();

    // Refresh highlight on an object (e.g., after Disable/Enable cycle destroys 3D)
    // Only applies highlight if the object is currently selected
    void RefreshHighlightIfSelected(RE::TESObjectREFR* ref);

    // Selection change callback (for undo/redo recording)
    using SelectionChangeCallback = std::function<void(const std::vector<SelectionInfo>& oldSelection,
                                                        const std::vector<SelectionInfo>& newSelection)>;
    void SetSelectionChangeCallback(SelectionChangeCallback callback) { m_changeCallback = callback; }

    // Suppress callback during undo/redo operations to avoid recording changes
    void SetSuppressCallback(bool suppress) { m_suppressCallback = suppress; }

    // Highlight colors
    static constexpr RE::NiColor kHoverColor{0.2f, 0.8f, 1.0f};      // Cyan for hover
    static constexpr RE::NiColor kSelectedColor{1.0f, 0.8f, 0.2f};   // Gold for selected

private:
    SelectionState() = default;
    ~SelectionState() = default;
    SelectionState(const SelectionState&) = delete;
    SelectionState& operator=(const SelectionState&) = delete;

    // Internal helpers
    SelectionInfo CreateSelectionInfo(RE::TESObjectREFR* ref);
    void ApplyHighlight(RE::TESObjectREFR* ref);
    void RemoveHighlight(RE::TESObjectREFR* ref);
    void NotifySelectionChange(const std::vector<SelectionInfo>& oldSelection);

    bool m_initialized = false;

    // Single unified selection list
    std::vector<SelectionInfo> m_selection;

    // Callback for undo/redo
    SelectionChangeCallback m_changeCallback;
    bool m_suppressCallback = false;
};

} // namespace Selection
