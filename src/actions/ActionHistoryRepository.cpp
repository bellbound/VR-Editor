#include "ActionHistoryRepository.h"
#include "../log.h"
#include "../persistence/ChangedObjectRegistry.h"
#include "../persistence/FormKeyUtil.h"
#include <RE/A/Actor.h>

namespace Actions {

namespace {
    // Helper to format form ID as hex string
    std::string FormatFormID(RE::FormID formId) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%08X", formId);
        return buffer;
    }

    // Helper to get a usable identifier for a form (editor ID preferred, else form ID)
    std::string GetFormIdentifier(RE::TESForm* form) {
        if (!form) return "";

        // Prefer editor ID
        const char* editorId = form->GetFormEditorID();
        if (editorId && editorId[0] != '\0') {
            return editorId;
        }

        // Fall back to form ID
        return FormatFormID(form->GetFormID());
    }

    // Get location name for a reference (for BOS INI file grouping)
    std::string GetLocationName(RE::TESObjectREFR* ref) {
        if (!ref) return "Unknown";

        // Try current location first
        auto* location = ref->GetCurrentLocation();
        if (location) {
            const char* name = location->GetFullName();
            if (name && name[0] != '\0') {
                return name;
            }

            // Location exists but no display name - use editor ID or form ID
            std::string identifier = GetFormIdentifier(location);
            if (!identifier.empty()) {
                return identifier;
            }
        }

        // Try parent cell
        auto* cell = ref->GetParentCell();
        if (cell) {
            // Try cell's parent location display name + cell form ID
            auto* cellLocation = cell->GetLocation();
            if (cellLocation) {
                const char* cellLocName = cellLocation->GetFullName();
                if (cellLocName && cellLocName[0] != '\0') {
                    return std::string(cellLocName) + "_" + FormatFormID(cell->GetFormID());
                }
            }

            // Use cell's identifier (editor ID or form ID)
            std::string cellIdentifier = GetFormIdentifier(cell);
            if (!cellIdentifier.empty()) {
                return cellIdentifier;
            }
        }

        return "Unknown";
    }

    // Check if a form ID refers to an Actor (NPC) — actors are excluded from persistence
    bool IsActor(RE::FormID formId) {
        auto* form = RE::TESForm::LookupByID(formId);
        return form && form->As<RE::Actor>() != nullptr;
    }

    // Helper to register changed objects from an action with the ChangedObjectRegistry
    // Only TransformAction, MultiTransformAction, and DeleteAction modify object state
    // SelectionAction is skipped - it doesn't change the object itself
    void RegisterChangedObjectsFromAction(const ActionData& action, const Util::ActionId& actionId) {
        auto* registry = Persistence::ChangedObjectRegistry::GetSingleton();

        std::visit([&](const auto& act) {
            using T = std::decay_t<decltype(act)>;

            if constexpr (std::is_same_v<T, TransformAction>) {
                // Single transform - register the one object (skip actors)
                if (!IsActor(act.formId)) {
                    if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(act.formId)) {
                        registry->RegisterIfNew(ref, act.initialTransform, actionId);
                    }
                }
            } else if constexpr (std::is_same_v<T, MultiTransformAction>) {
                // Multi-transform - register each object
                for (const auto& st : act.transforms) {
                    if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(st.formId)) {
                        registry->RegisterIfNew(ref, st.initialTransform, actionId);
                    }
                }
            } else if constexpr (std::is_same_v<T, DeleteAction>) {
                // Delete - register each deleted object with base form info
                for (const auto& del : act.deletedObjects) {
                    if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(del.formId)) {
                        registry->RegisterDeletedIfNew(ref, del.baseFormId, del.transform, actionId);
                    }
                }
            }
            // SelectionAction: Skip - doesn't modify object state
        }, action);
    }

    // Helper to update current transforms in the registry for BOS export
    // Called when actions are added, undone, or redone
    // useChangedTransform: true = use changedTransform (add/redo), false = use initialTransform (undo)
    void UpdateCurrentTransformsFromAction(const ActionData& action, bool useChangedTransform) {
        auto* registry = Persistence::ChangedObjectRegistry::GetSingleton();

        std::visit([&](const auto& act) {
            using T = std::decay_t<decltype(act)>;

            if constexpr (std::is_same_v<T, TransformAction>) {
                // Skip actors — NPC moves are not persisted
                if (!IsActor(act.formId)) {
                    if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(act.formId)) {
                        std::string formKey = Persistence::FormKeyUtil::BuildFormKey(ref);
                        if (!formKey.empty()) {
                            const auto& transform = useChangedTransform ? act.changedTransform : act.initialTransform;
                            registry->UpdateCurrentTransform(formKey, transform, GetLocationName(ref));
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, MultiTransformAction>) {
                for (const auto& st : act.transforms) {
                    if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(st.formId)) {
                        std::string formKey = Persistence::FormKeyUtil::BuildFormKey(ref);
                        if (!formKey.empty()) {
                            const auto& transform = useChangedTransform ? st.changedTransform : st.initialTransform;
                            registry->UpdateCurrentTransform(formKey, transform, GetLocationName(ref));
                        }
                    }
                }
            }
            // DeleteAction: No transform to update (object is deleted/restored)
            // SelectionAction: No transform changes
        }, action);
    }
} // anonymous namespace

ActionHistoryRepository* ActionHistoryRepository::GetSingleton()
{
    static ActionHistoryRepository instance;
    return &instance;
}

Util::ActionId ActionHistoryRepository::Add(const ActionData& action)
{
    ClearRedoStack();  // New action invalidates redo history
    Util::ActionId id = GetActionId(action);
    m_actions.emplace(id, action);

    // Register changed objects with persistence system
    RegisterChangedObjectsFromAction(action, id);

    // Update current transforms for BOS export
    UpdateCurrentTransformsFromAction(action, true);  // true = use changedTransform

    spdlog::trace("ActionHistoryRepository: Added action {}", id.ToString());
    return id;
}

Util::ActionId ActionHistoryRepository::Add(ActionData&& action)
{
    ClearRedoStack();  // New action invalidates redo history
    Util::ActionId id = GetActionId(action);

    // Register before moving (need to read the action data)
    RegisterChangedObjectsFromAction(action, id);

    // Update current transforms for BOS export (before moving)
    UpdateCurrentTransformsFromAction(action, true);  // true = use changedTransform

    m_actions.emplace(id, std::move(action));
    spdlog::trace("ActionHistoryRepository: Added action {}", id.ToString());
    return id;
}

Util::ActionId ActionHistoryRepository::AddTransform(RE::FormID formId,
                                                      const RE::NiTransform& initial,
                                                      const RE::NiTransform& changed,
                                                      const RE::NiPoint3& initialEuler,
                                                      const RE::NiPoint3& changedEuler)
{
    ClearRedoStack();  // New action invalidates redo history
    // Use the Euler angle constructor for lossless undo/redo
    TransformAction action(formId, initial, changed, initialEuler, changedEuler);
    Util::ActionId id = action.actionId;

    // Register with persistence before storing (skip actors — NPC moves are not persisted)
    if (!IsActor(formId)) {
        if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId)) {
            auto* registry = Persistence::ChangedObjectRegistry::GetSingleton();
            registry->RegisterIfNew(ref, initial, id);

            // Update current transform for BOS export
            std::string formKey = Persistence::FormKeyUtil::BuildFormKey(ref);
            if (!formKey.empty()) {
                registry->UpdateCurrentTransform(formKey, changed, GetLocationName(ref));
            }
        }
    }

    m_actions.emplace(id, std::move(action));

    spdlog::trace("ActionHistoryRepository: Added transform action {} for form {:08X}",
        id.ToString(), formId);
    return id;
}

Util::ActionId ActionHistoryRepository::AddMultiTransform(std::vector<SingleTransform>&& transforms)
{
    if (transforms.empty()) {
        spdlog::trace("ActionHistoryRepository: Skipping empty multi-transform");
        return Util::ActionId{};
    }

    ClearRedoStack();  // New action invalidates redo history
    MultiTransformAction action(std::move(transforms));
    Util::ActionId id = action.actionId;

    // Register each transformed object with persistence and update current transforms
    auto* registry = Persistence::ChangedObjectRegistry::GetSingleton();
    for (const auto& st : action.transforms) {
        if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(st.formId)) {
            registry->RegisterIfNew(ref, st.initialTransform, id);

            // Update current transform for BOS export
            std::string formKey = Persistence::FormKeyUtil::BuildFormKey(ref);
            if (!formKey.empty()) {
                registry->UpdateCurrentTransform(formKey, st.changedTransform, GetLocationName(ref));
            }
        }
    }

    spdlog::trace("ActionHistoryRepository: Added multi-transform action {} ({} objects)",
        id.ToString(), action.transforms.size());

    m_actions.emplace(id, std::move(action));
    return id;
}

Util::ActionId ActionHistoryRepository::AddSelection(const std::vector<RE::FormID>& previousSelection,
                                                      const std::vector<RE::FormID>& newSelection)
{
    ClearRedoStack();  // New action invalidates redo history
    SelectionAction action(previousSelection, newSelection);
    Util::ActionId id = action.actionId;
    m_actions.emplace(id, std::move(action));

    spdlog::trace("ActionHistoryRepository: Added selection action {} (prev: {}, new: {})",
        id.ToString(), previousSelection.size(), newSelection.size());
    return id;
}

std::optional<ActionData> ActionHistoryRepository::Get(const Util::ActionId& id) const
{
    auto it = m_actions.find(id);
    if (it != m_actions.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<ActionData> ActionHistoryRepository::GetLast() const
{
    if (m_actions.empty()) {
        return std::nullopt;
    }
    return m_actions.rbegin()->second;
}

bool ActionHistoryRepository::Remove(const Util::ActionId& id)
{
    auto it = m_actions.find(id);
    if (it != m_actions.end()) {
        m_actions.erase(it);
        spdlog::trace("ActionHistoryRepository: Removed action {}", id.ToString());
        return true;
    }
    return false;
}

void ActionHistoryRepository::RemoveAfter(const Util::ActionId& id)
{
    auto it = m_actions.upper_bound(id);
    if (it != m_actions.end()) {
        size_t count = std::distance(it, m_actions.end());
        m_actions.erase(it, m_actions.end());
        spdlog::trace("ActionHistoryRepository: Removed {} actions after {}", count, id.ToString());
    }
}

void ActionHistoryRepository::Clear()
{
    size_t count = m_actions.size();
    size_t redoCount = m_redoStack.size();
    m_actions.clear();
    m_redoStack.clear();
    spdlog::trace("ActionHistoryRepository: Cleared {} actions and {} redo entries", count, redoCount);
}

size_t ActionHistoryRepository::Count() const
{
    return m_actions.size();
}

bool ActionHistoryRepository::IsEmpty() const
{
    return m_actions.empty();
}

bool ActionHistoryRepository::CanUndo() const
{
    return !m_actions.empty();
}

bool ActionHistoryRepository::CanRedo() const
{
    return !m_redoStack.empty();
}

bool ActionHistoryRepository::HasUserVisibleUndo() const
{
    // Iterate from newest to oldest, looking for a user-visible action
    for (auto it = m_actions.rbegin(); it != m_actions.rend(); ++it) {
        if (IsUserVisibleAction(it->second)) {
            return true;
        }
    }
    return false;
}

bool ActionHistoryRepository::HasUserVisibleRedo() const
{
    // Iterate from newest to oldest (back of vector), looking for a user-visible action
    for (auto it = m_redoStack.rbegin(); it != m_redoStack.rend(); ++it) {
        if (IsUserVisibleAction(*it)) {
            return true;
        }
    }
    return false;
}

std::optional<ActionData> ActionHistoryRepository::Undo()
{
    if (m_actions.empty()) {
        spdlog::trace("ActionHistoryRepository: Nothing to undo");
        return std::nullopt;
    }

    // Get the most recent action (last in the ordered map)
    auto it = m_actions.rbegin();
    ActionData action = it->second;
    Util::ActionId id = it->first;

    // Move to redo stack
    m_redoStack.push_back(std::move(action));

    // Remove from actions map (need to convert reverse iterator)
    m_actions.erase(std::prev(m_actions.end()));

    // Update current transforms for BOS export (use initial transforms - undoing)
    UpdateCurrentTransformsFromAction(m_redoStack.back(), false);  // false = use initialTransform

    spdlog::info("ActionHistoryRepository: Undid action {} (undo: {}, redo: {})",
        id.ToString(), m_actions.size(), m_redoStack.size());

    return m_redoStack.back();
}

std::optional<ActionData> ActionHistoryRepository::Redo()
{
    if (m_redoStack.empty()) {
        spdlog::trace("ActionHistoryRepository: Nothing to redo");
        return std::nullopt;
    }

    // Get the most recently undone action
    ActionData action = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    // Re-add to the actions map
    Util::ActionId id = GetActionId(action);
    m_actions.emplace(id, action);

    // Update current transforms for BOS export (use changed transforms - redoing)
    UpdateCurrentTransformsFromAction(action, true);  // true = use changedTransform

    spdlog::info("ActionHistoryRepository: Redid action {} (undo: {}, redo: {})",
        id.ToString(), m_actions.size(), m_redoStack.size());

    return action;
}

void ActionHistoryRepository::ClearRedoStack()
{
    if (!m_redoStack.empty()) {
        spdlog::trace("ActionHistoryRepository: Cleared {} redo entries (new action performed)",
            m_redoStack.size());
        m_redoStack.clear();
    }
}

} // namespace Actions
