#pragma once

#include "Action.h"
#include <map>
#include <optional>
#include <vector>

namespace Actions {

// ActionHistoryRepository: Stores action history for undo/redo functionality
//
// Design notes:
// - Uses std::map<ActionId, ActionData> which maintains insertion order by ActionId
// - Since ActionId contains a monotonic counter in upper bits, this naturally
//   preserves chronological order
// - Each action is uniquely identified and can be looked up, removed, or iterated
//
// Undo/Redo implementation:
// - Main map holds "done" actions, m_redoStack holds "undone" actions
// - Undo: pops from main map, pushes to redo stack
// - Redo: pops from redo stack, pushes back to main map
// - New actions clear the redo stack (standard undo behavior)
class ActionHistoryRepository {
public:
    static ActionHistoryRepository* GetSingleton();

    // Add an action to history
    // Returns the ActionId for future reference
    // NOTE: Adding a new action clears the redo stack
    Util::ActionId Add(const ActionData& action);
    Util::ActionId Add(ActionData&& action);

    // Convenience method for adding a TransformAction with Euler angles
    // NOTE: Adding a new action clears the redo stack
    Util::ActionId AddTransform(RE::FormID formId,
                                const RE::NiTransform& initial,
                                const RE::NiTransform& changed,
                                const RE::NiPoint3& initialEuler,
                                const RE::NiPoint3& changedEuler);

    // Convenience method for adding a MultiTransformAction (group move)
    // NOTE: Adding a new action clears the redo stack
    Util::ActionId AddMultiTransform(std::vector<SingleTransform>&& transforms);

    // Convenience method for adding a SelectionAction
    // NOTE: Adding a new action clears the redo stack
    Util::ActionId AddSelection(const std::vector<RE::FormID>& previousSelection,
                                const std::vector<RE::FormID>& newSelection);

    // Get an action by ID
    std::optional<ActionData> Get(const Util::ActionId& id) const;

    // Get the most recent action
    std::optional<ActionData> GetLast() const;

    // Remove an action by ID
    bool Remove(const Util::ActionId& id);

    // Remove all actions after a given ID (for implementing redo truncation)
    void RemoveAfter(const Util::ActionId& id);

    // Clear all history (including redo stack)
    void Clear();

    // Get the number of actions in history
    size_t Count() const;

    // Check if history is empty
    bool IsEmpty() const;

    // ========== Undo/Redo Support ==========

    // Check if undo is available
    bool CanUndo() const;

    // Check if redo is available
    bool CanRedo() const;

    // Undo the most recent action
    // Returns the action that was undone (to apply its inverse)
    // Returns nullopt if nothing to undo
    std::optional<ActionData> Undo();

    // Redo the most recently undone action
    // Returns the action that was redone (to reapply it)
    // Returns nullopt if nothing to redo
    std::optional<ActionData> Redo();

    // Get undo/redo stack sizes (for debugging/UI)
    size_t UndoCount() const { return m_actions.size(); }
    size_t RedoCount() const { return m_redoStack.size(); }

    // Check if there's a user-visible action to undo
    // (Skips single-select actions which are internal-only)
    bool HasUserVisibleUndo() const;

    // Check if there's a user-visible action to redo
    // (Skips single-select actions which are internal-only)
    bool HasUserVisibleRedo() const;

    // Iterate over all actions (oldest to newest)
    // Callback receives (actionId, actionData) and returns true to continue, false to stop
    template<typename Func>
    void ForEach(Func&& callback) const {
        for (const auto& [id, data] : m_actions) {
            if (!callback(id, data)) {
                break;
            }
        }
    }

    // Iterate over all actions in reverse (newest to oldest)
    template<typename Func>
    void ForEachReverse(Func&& callback) const {
        for (auto it = m_actions.rbegin(); it != m_actions.rend(); ++it) {
            if (!callback(it->first, it->second)) {
                break;
            }
        }
    }

private:
    ActionHistoryRepository() = default;
    ~ActionHistoryRepository() = default;
    ActionHistoryRepository(const ActionHistoryRepository&) = delete;
    ActionHistoryRepository& operator=(const ActionHistoryRepository&) = delete;

    // Clears redo stack when new actions are performed
    void ClearRedoStack();

    // Map of ActionId -> ActionData, ordered by ActionId (chronological)
    std::map<Util::ActionId, ActionData> m_actions;

    // Stack of undone actions (for redo functionality)
    // Most recently undone action is at the back
    std::vector<ActionData> m_redoStack;
};

} // namespace Actions
