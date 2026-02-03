#pragma once

#include "Action.h"
#include "ActionHistoryRepository.h"
#include "../selection/SelectionState.h"
#include "../visuals/ObjectHighlighter.h"
#include "../persistence/ChangedObjectRegistry.h"
#include "../persistence/CreatedObjectTracker.h"
#include "../persistence/FormKeyUtil.h"
#include "../log.h"
#include <vector>
#include <fmt/format.h>

namespace Actions {

// DeleteHandler: Handles deletion of selected objects using Disable()
// Objects are hidden but remain in the game, allowing undo via Enable()
class DeleteHandler
{
public:
    static DeleteHandler* GetSingleton()
    {
        static DeleteHandler instance;
        return &instance;
    }

    // Delete all currently selected objects (disables them)
    // Returns true if any objects were deleted
    bool DeleteSelection()
    {
        auto* selectionState = Selection::SelectionState::GetSingleton();
        auto selection = selectionState->GetSelection();

        if (selection.empty()) {
            spdlog::info("DeleteHandler: No objects selected to delete");
            return false;
        }

        auto* registry = Persistence::ChangedObjectRegistry::GetSingleton();
        std::vector<SingleDelete> deletedObjects;
        deletedObjects.reserve(selection.size());

        for (const auto& info : selection) {
            if (!info.ref) continue;

            SingleDelete del;
            del.formId = info.formId;

            // Get base form for registry
            auto* baseObj = info.ref->GetBaseObject();
            del.baseFormId = baseObj ? baseObj->GetFormID() : 0;

            // Store current transform for undo
            if (auto* node = info.ref->Get3D()) {
                del.transform = node->world;
            } else {
                del.transform.translate = info.ref->GetPosition();
                del.transform.scale = 1.0f;
            }

            // Check if this is a dynamic (runtime-created) reference
            std::string formKey = Persistence::FormKeyUtil::BuildFormKey(info.ref);
            bool isDynamic = formKey.empty();

            // Register deletion in ChangedObjectRegistry (for persistence/tracking)
            Util::ActionId actionId = Util::UUID::Generate();
            registry->RegisterDeletedIfNew(info.ref, del.baseFormId, del.transform, actionId);

            // Unhighlight before deletion (use FormID for physics objects)
            ObjectHighlighter::UnhighlightByFormId(info.formId);

            // Disable the object - hides it but keeps it in the game for undo
            info.ref->Disable();

            // Dynamic refs (copies/gallery): mark for hard delete on next load
            // Plugin refs: only disabled, can be re-enabled
            if (isDynamic) {
                std::string dynamicKey = fmt::format("0x{:08X}~DYNAMIC", info.formId);
                registry->MarkPendingHardDelete(dynamicKey);

                // Remove from CreatedObjectTracker so it won't be respawned
                Persistence::CreatedObjectTracker::GetSingleton()->Remove(info.ref);

                spdlog::info("DeleteHandler: Disabled dynamic ref {:08X}, marked for hard delete, removed from tracker", info.formId);
            } else {
                spdlog::info("DeleteHandler: Disabled plugin ref {:08X}", info.formId);
            }

            deletedObjects.push_back(del);
        }

        if (deletedObjects.empty()) {
            return false;
        }

        // Record action for undo
        DeleteAction action(std::move(deletedObjects));
        ActionHistoryRepository::GetSingleton()->Add(std::move(action));

        // Clear selection (objects are "gone")
        selectionState->ClearAll();

        spdlog::info("DeleteHandler: Disabled {} objects", deletedObjects.size());
        return true;
    }

private:
    DeleteHandler() = default;
    ~DeleteHandler() = default;
    DeleteHandler(const DeleteHandler&) = delete;
    DeleteHandler& operator=(const DeleteHandler&) = delete;
};

} // namespace Actions
