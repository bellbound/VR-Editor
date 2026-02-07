#include "VRBuilderNativePapyrusAPI.h"
#include "../EditModeManager.h"
#include "../util/PositioningUtil.h"
#include "../visuals/ObjectHighlighter.h"
#include "../ui/SelectionMenu.h"
#include "../ui/GalleryMenu.h"
#include "../persistence/AddedObjectsParser.h"
#include "../persistence/AddedObjectsSpawner.h"
#include "../persistence/BaseObjectSwapperParser.h"
#include "../persistence/ChangedObjectRegistry.h"
#include "../persistence/CreatedObjectTracker.h"
#include "../persistence/FormKeyUtil.h"
#include "../log.h"
#include <fmt/format.h>
#include <RE/P/PlayerCharacter.h>
#include <filesystem>
#include <vector>

namespace API {
namespace VRBuilderNativePapyrus {

void ToggleEditMode(RE::StaticFunctionTag*)
{
    auto* mgr = EditModeManager::GetSingleton();
    if (mgr->IsInEditMode()) {
        // Exit edit mode
        ObjectHighlighter::UnhighlightAll();
        SelectionMenu::GetSingleton()->OnEditModeExit();
        GalleryMenu::GetSingleton()->OnEditModeExit();
        mgr->Exit();
        spdlog::info("VRBuilderNativePapyrusAPI: Exited edit mode via ToggleEditMode");
    } else {
        // Enter edit mode
        mgr->Enter();
        SelectionMenu::GetSingleton()->OnEditModeEnter();
        spdlog::info("VRBuilderNativePapyrusAPI: Entered edit mode via ToggleEditMode");
    }
}

bool IsInEditMode(RE::StaticFunctionTag*)
{
    return EditModeManager::GetSingleton()->IsInEditMode();
}

void ResetCurrentCellEdits(RE::StaticFunctionTag*)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn("VRBuilderNativePapyrusAPI: ResetCurrentCellEdits - player not found");
        return;
    }

    auto* cell = player->GetParentCell();
    if (!cell) {
        spdlog::warn("VRBuilderNativePapyrusAPI: ResetCurrentCellEdits - cell not found");
        return;
    }

    std::string cellFormKey = Persistence::FormKeyUtil::BuildFormKey(cell);
    std::string cellEditorId = cell->GetFormEditorID() ? cell->GetFormEditorID() : "";

    std::string cellName;
    if (const char* name = cell->GetName(); name && name[0] != '\0') {
        cellName = name;
    } else if (!cellEditorId.empty()) {
        cellName = cellEditorId;
    } else {
        cellName = fmt::format("{:08X}", cell->GetFormID());
    }

    RE::DebugNotification(fmt::format("VR Editor: Resetting {}...", cellName).c_str());

    auto* registry = Persistence::ChangedObjectRegistry::GetSingleton();
    auto entries = registry->ExtractEntriesForCell(cellFormKey);

    size_t resetCount = 0;
    std::vector<std::string> createdFormKeys;

    for (auto& [formKey, data] : entries) {
        if (data.saveData.wasCreated) {
            createdFormKeys.push_back(formKey);
            continue;
        }

        RE::FormID runtimeFormId = Persistence::FormKeyUtil::ResolveToRuntimeFormID(formKey);
        if (runtimeFormId == 0) {
            continue;
        }

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(runtimeFormId);
        if (!ref) {
            continue;
        }

        if (ref->IsDeleted()) {
            ref->SetDelete(false);
        }

        ref->Enable(false);

        // Apply original transform
        RE::NiPoint3 angles = PositioningUtil::MatrixToEulerAngles(data.saveData.originalTransform.rotate);
        PositioningUtil::SetPositionNative(ref, data.saveData.originalTransform.translate);
        PositioningUtil::SetAngleNative(ref, angles);
        ref->SetScale(data.saveData.originalTransform.scale);
        ref->Update3DPosition(true);

        // Force Havok to rebuild collision and refresh 3D
        ref->Disable();
        ref->Enable(false);

        resetCount++;
    }

    // Remove and delete added objects for this cell
    size_t removedAddedCount = 0;
    auto* tracker = Persistence::CreatedObjectTracker::GetSingleton();
    if (tracker) {
        removedAddedCount = tracker->RemoveForCell(cellFormKey);
    }

    // Ensure any created refs not tracked are deleted
    for (const auto& formKey : createdFormKeys) {
        RE::FormID runtimeFormId = Persistence::FormKeyUtil::ResolveToRuntimeFormID(formKey);
        if (runtimeFormId == 0) {
            continue;
        }

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(runtimeFormId);
        if (!ref) {
            continue;
        }

        ref->Disable();
        ref->SetDelete(true);
    }

    if (removedAddedCount < createdFormKeys.size()) {
        removedAddedCount = createdFormKeys.size();
    }

    auto* spawner = Persistence::AddedObjectsSpawner::GetSingleton();
    if (spawner) {
        spawner->RemoveCellEntries(cellFormKey);
    }

    // Remove AddedObjects INI for this cell
    {
        auto* parser = Persistence::AddedObjectsParser::GetSingleton();
        auto fileName = Persistence::AddedObjectsParser::BuildIniFileName(cellEditorId, cellFormKey);
        auto filePath = parser->GetVREditorFolderPath() / fileName;
        std::error_code ec;
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath, ec);
            if (ec) {
                spdlog::warn("VRBuilderNativePapyrusAPI: Failed to delete {}", filePath.string());
            }
        }
    }

    // Remove BOS swap/session files for this cell
    {
        auto* bos = Persistence::BaseObjectSwapperParser::GetSingleton();
        auto swapFileName = Persistence::BaseObjectSwapperParser::BuildIniFileName(cellEditorId, cellFormKey);
        auto sessionFileName = Persistence::BaseObjectSwapperParser::BuildSessionIniFileName(cellEditorId, cellFormKey);

        auto swapPath = bos->GetDataFolderPath() / swapFileName;
        auto sessionPath = bos->GetVREditorFolderPath() / sessionFileName;

        std::error_code ec;
        if (std::filesystem::exists(swapPath)) {
            std::filesystem::remove(swapPath, ec);
            if (ec) {
                spdlog::warn("VRBuilderNativePapyrusAPI: Failed to delete {}", swapPath.string());
            }
        }
        if (std::filesystem::exists(sessionPath)) {
            std::filesystem::remove(sessionPath, ec);
            if (ec) {
                spdlog::warn("VRBuilderNativePapyrusAPI: Failed to delete {}", sessionPath.string());
            }
        }
    }

    // NOTE: Spriggit export feature removed - no partials to clean up

    std::string message = fmt::format("VR Editor: Reset {} objects", resetCount);
    if (removedAddedCount > 0) {
        message += fmt::format(", removed {} added objects", removedAddedCount);
    }
    RE::DebugNotification(message.c_str());
}

bool Bind(VM* a_vm)
{
    if (!a_vm) {
        spdlog::error("VRBuilderNativePapyrusAPI::Bind: VM is null");
        return false;
    }

    constexpr auto scriptName = "VRBuilderNative"sv;

    a_vm->RegisterFunction("ToggleEditMode"sv, scriptName, ToggleEditMode);
    a_vm->RegisterFunction("IsInEditMode"sv, scriptName, IsInEditMode);
    a_vm->RegisterFunction("ResetCurrentCellEdits"sv, scriptName, ResetCurrentCellEdits);

    spdlog::info("VRBuilderNativePapyrusAPI: Registered native functions for '{}'", scriptName);
    return true;
}

} // namespace VRBuilderNativePapyrus
} // namespace API
