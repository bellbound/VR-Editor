#pragma once

#include "../util/UUID.h"
#include <RE/N/NiTransform.h>
#include <RE/F/FormTypes.h>
#include <variant>
#include <vector>

namespace Actions {

// Base action metadata - all actions share these fields
struct ActionBase {
    Util::ActionId actionId;  // Unique identifier for this action
};

// TransformAction: Records a change to an object's position/rotation/scale
// Used for tracking object movements so they can be undone/redone
struct TransformAction : ActionBase {
    RE::FormID formId;                  // The object that was transformed
    RE::NiTransform initialTransform;   // Transform before the action
    RE::NiTransform changedTransform;   // Transform after the action

    // Euler angles for lossless undo/redo
    // ApplyTransform uses these directly, avoiding the lossy Matrix→Euler conversion.
    RE::NiPoint3 initialEulerAngles;    // Euler angles before the action
    RE::NiPoint3 changedEulerAngles;    // Euler angles after the action

    TransformAction() : formId(0) {}

    // Constructor with Euler angles (required for lossless undo/redo)
    TransformAction(RE::FormID form, const RE::NiTransform& initial, const RE::NiTransform& changed,
                    const RE::NiPoint3& initialEuler, const RE::NiPoint3& changedEuler)
        : formId(form), initialTransform(initial), changedTransform(changed),
          initialEulerAngles(initialEuler), changedEulerAngles(changedEuler)
    {
        actionId = Util::UUID::Generate();
    }
};

// SingleTransform: One object's before/after transform (used in MultiTransformAction)
struct SingleTransform {
    RE::FormID formId;
    RE::NiTransform initialTransform;
    RE::NiTransform changedTransform;

    // Euler angles for lossless undo/redo
    // ApplyTransform uses these directly, avoiding the lossy Matrix→Euler conversion.
    RE::NiPoint3 initialEulerAngles;   // Euler angles before the action
    RE::NiPoint3 changedEulerAngles;   // Euler angles after the action
};

// MultiTransformAction: Records changes to multiple objects as a single undo entry
// Used for group moves where all objects should undo/redo together
struct MultiTransformAction : ActionBase {
    std::vector<SingleTransform> transforms;

    MultiTransformAction() = default;

    explicit MultiTransformAction(std::vector<SingleTransform>&& t)
        : transforms(std::move(t))
    {
        actionId = Util::UUID::Generate();
    }
};

// SelectionAction: Records a change to the selection state
// Used for undo/redo of selection changes
struct SelectionAction : ActionBase {
    std::vector<RE::FormID> previousSelection;  // Selection before the action
    std::vector<RE::FormID> newSelection;       // Selection after the action

    SelectionAction() = default;

    SelectionAction(const std::vector<RE::FormID>& prev, const std::vector<RE::FormID>& current)
        : previousSelection(prev), newSelection(current)
    {
        actionId = Util::UUID::Generate();
    }
};

// SingleDelete: One object's deletion info (for multi-delete undo)
struct SingleDelete {
    RE::FormID formId;
    RE::FormID baseFormId;          // The base form to recreate the object
    RE::NiTransform transform;      // Position/rotation/scale at deletion
};

// DeleteAction: Records deletion of one or more objects
// Used for undo (recreate) / redo (delete again)
struct DeleteAction : ActionBase {
    std::vector<SingleDelete> deletedObjects;

    DeleteAction() = default;

    explicit DeleteAction(std::vector<SingleDelete>&& objects)
        : deletedObjects(std::move(objects))
    {
        actionId = Util::UUID::Generate();
    }
};

// SingleCopy: One object's copy info (for multi-copy undo)
struct SingleCopy {
    RE::FormID originalFormId;     // The original reference that was copied
    RE::FormID createdFormId;      // The newly created reference
    RE::NiTransform transform;     // Position/rotation/scale of the copy
};

// CopyAction: Records creation of one or more object copies
// Undo = delete the copies, Redo = recreate them
struct CopyAction : ActionBase {
    std::vector<SingleCopy> copiedObjects;

    CopyAction() = default;

    explicit CopyAction(std::vector<SingleCopy>&& objects)
        : copiedObjects(std::move(objects))
    {
        actionId = Util::UUID::Generate();
    }
};

// ActionType enum for runtime type identification
enum class ActionType : uint8_t {
    Transform,
    MultiTransform,
    Selection,
    Delete,
    Copy,
};

// ActionData: Type-safe variant holding any action type's data
// Using std::variant allows different action types while maintaining type safety
using ActionData = std::variant<TransformAction, MultiTransformAction, SelectionAction, DeleteAction, CopyAction>;

// Helper to get ActionType from ActionData
inline ActionType GetActionType(const ActionData& data) {
    return static_cast<ActionType>(data.index());
}

// Helper to get action ID from any action in the variant
inline Util::ActionId GetActionId(const ActionData& data) {
    return std::visit([](const auto& action) { return action.actionId; }, data);
}

// Helper to determine if an action is "user-visible" for undo/redo purposes
// Only multi-selection UNSELECT operations are user-visible for selection actions
// (i.e., when user clears/changes a selection that had multiple items)
// All non-selection actions are always user-visible
inline bool IsUserVisibleAction(const ActionData& data) {
    if (auto* selAction = std::get_if<SelectionAction>(&data)) {
        // A selection action is user-visible only when unselecting from multi-select
        // i.e., previous selection had more than 1 item (user had N objects selected)
        return selAction->previousSelection.size() > 1;
    }
    // All other action types are always user-visible
    return true;
}

} // namespace Actions
