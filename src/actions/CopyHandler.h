#pragma once

#include "Action.h"
#include "ActionHistoryRepository.h"
#include "../selection/SelectionState.h"
#include "../visuals/ObjectHighlighter.h"
#include "../persistence/ChangedObjectRegistry.h"
#include "../persistence/CreatedObjectTracker.h"
#include "../persistence/FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESFullName.h>
#include <RE/M/Misc.h>
#include <vector>

namespace Actions {

// CopyHandler: Handles duplication of selected objects
// Records copies to undo history for undo/redo support
// Registers created objects with ChangedObjectRegistry for persistence tracking
class CopyHandler
{
public:
    static CopyHandler* GetSingleton()
    {
        static CopyHandler instance;
        return &instance;
    }

    // Duplicate all currently selected objects
    // Places copies slightly offset from originals
    // Returns true if any objects were copied
    bool CopySelection()
    {
        auto* selectionState = Selection::SelectionState::GetSingleton();
        auto selection = selectionState->GetSelection();

        if (selection.empty()) {
            spdlog::info("CopyHandler: No objects selected to copy");
            return false;
        }

        std::vector<SingleCopy> copiedObjects;
        copiedObjects.reserve(selection.size());

        std::vector<RE::FormID> newSelectionFormIds;
        newSelectionFormIds.reserve(selection.size());

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            spdlog::error("CopyHandler: TESDataHandler not available");
            return false;
        }

        // Generate action ID upfront for registry registration
        Util::ActionId actionId = Util::UUID::Generate();

        auto* registry = Persistence::ChangedObjectRegistry::GetSingleton();

        for (const auto& info : selection) {
            if (!info.ref) continue;

            // Skip actors - copying them causes issues with AI, scripts, and quests
            if (info.ref->As<RE::Actor>()) {
                spdlog::info("CopyHandler: Skipping actor {:08X} (actors cannot be duplicated)", info.formId);
                continue;
            }

            auto* baseObj = info.ref->GetBaseObject();
            if (!baseObj) {
                spdlog::warn("CopyHandler: Object {:08X} has no base object, skipping", info.formId);
                continue;
            }

            // Get original transform (including scale)
            RE::NiPoint3 position = info.ref->GetPosition();
            RE::NiPoint3 angle = info.ref->GetAngle();
            float originalScale = 1.0f;
            if (auto* node3D = info.ref->Get3D()) {
                originalScale = node3D->world.scale;
            }

            // Apply offset so copy doesn't overlap original
            // Offset along X-axis for visibility
            position.x += kCopyOffset;

            // Create the copy using TESDataHandler
            auto newRefHandle = dataHandler->CreateReferenceAtLocation(
                baseObj,
                position,
                angle,
                info.ref->GetParentCell(),
                info.ref->GetWorldspace(),
                nullptr,  // a_alreadyCreatedRef
                nullptr,  // a_primitive
                RE::ObjectRefHandle(),  // a_linkedRoomRefHandle
                true,     // a_forcePersist - let game handle persistence
                true      // a_arg11
            );

            auto newRef = newRefHandle.get();
            if (!newRef) {
                spdlog::error("CopyHandler: Failed to create copy of {:08X}", info.formId);
                continue;
            }

            // Apply the original object's scale to the copy
            if (originalScale != 1.0f) {
                newRef->SetScale(originalScale);
                spdlog::info("CopyHandler: Applied scale {:.2f} to copy {:08X}", originalScale, newRef->GetFormID());
            }

            // Build transform for recording
            RE::NiTransform transform;
            if (auto* node = newRef->Get3D()) {
                transform = node->world;
            } else {
                transform.translate = position;
                transform.scale = originalScale;
            }

            // Register with ChangedObjectRegistry as a created object (for INI export)
            registry->RegisterCreatedObject(newRef.get(), baseObj->GetFormID(), transform, actionId);

            // Register with CreatedObjectTracker for runtime spawning/despawning
            auto* cell = info.ref->GetParentCell();
            std::string cellFormKey = cell ? Persistence::FormKeyUtil::BuildFormKey(cell) : "";
            if (!cellFormKey.empty()) {
                Persistence::CreatedObjectTracker::GetSingleton()->Add(newRef.get(), baseObj->GetFormID(), cellFormKey);
            }

            // Record for undo
            SingleCopy copy;
            copy.originalFormId = info.formId;
            copy.createdFormId = newRef->GetFormID();
            copy.transform = transform;
            copiedObjects.push_back(copy);
            newSelectionFormIds.push_back(copy.createdFormId);

            spdlog::info("CopyHandler: Created copy {:08X} of {:08X} at ({:.1f}, {:.1f}, {:.1f})",
                copy.createdFormId, info.formId, position.x, position.y, position.z);

            // Debug notification with object name
            std::string objName = "object";
            if (auto* fullName = baseObj->As<RE::TESFullName>()) {
                if (const char* name = fullName->GetFullName(); name && name[0] != '\0') {
                    objName = name;
                }
            }
            std::string notification = "Copied " + objName;
            RE::DebugNotification(notification.c_str());
        }

        if (copiedObjects.empty()) {
            return false;
        }

        // Record action for undo (with the pre-generated actionId)
        CopyAction action(std::move(copiedObjects));
        action.actionId = actionId;  // Use the same ID we registered with
        ActionHistoryRepository::GetSingleton()->Add(std::move(action));

        // Unhighlight old selection (use FormID for physics objects)
        for (const auto& info : selection) {
            if (info.formId != 0) {
                ObjectHighlighter::UnhighlightByFormId(info.formId);
            }
        }

        // Clear old selection and select the new copies
        // Note: ObjectHighlighter automatically defers highlight if 3D isn't ready yet
        selectionState->ClearAll();
        for (auto formId : newSelectionFormIds) {
            if (auto* form = RE::TESForm::LookupByID(formId)) {
                if (auto* ref = form->AsReference()) {
                    selectionState->AddToSelection(ref);
                }
            }
        }

        spdlog::info("CopyHandler: Copied {} objects, now selected", newSelectionFormIds.size());
        return true;
    }

    // Offset applied to copies so they don't overlap originals
    static constexpr float kCopyOffset = 50.0f;

private:
    CopyHandler() = default;
    ~CopyHandler() = default;
    CopyHandler(const CopyHandler&) = delete;
    CopyHandler& operator=(const CopyHandler&) = delete;
};

} // namespace Actions
