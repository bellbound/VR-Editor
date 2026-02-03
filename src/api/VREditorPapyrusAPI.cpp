#include "VREditorPapyrusAPI.h"
#include "../EditModeManager.h"
#include "../EditModeStateManager.h"
#include "../visuals/ObjectHighlighter.h"
#include "../ui/SelectionMenu.h"
#include "../ui/GalleryMenu.h"
#include "../log.h"

namespace API {
namespace VREditorPapyrus {

bool IsInEditMode(RE::StaticFunctionTag*)
{
    return EditModeManager::GetSingleton()->IsInEditMode();
}

void EnterEditMode(RE::StaticFunctionTag*)
{
    auto* mgr = EditModeManager::GetSingleton();
    if (!mgr->IsInEditMode()) {
        mgr->Enter();
        SelectionMenu::GetSingleton()->OnEditModeEnter();
        spdlog::info("VREditorPapyrusAPI: Entered edit mode via Papyrus");
    }
}

void ExitEditMode(RE::StaticFunctionTag*)
{
    auto* mgr = EditModeManager::GetSingleton();
    if (mgr->IsInEditMode()) {
        ObjectHighlighter::UnhighlightAll();
        SelectionMenu::GetSingleton()->OnEditModeExit();
        GalleryMenu::GetSingleton()->OnEditModeExit();
        mgr->Exit();
        spdlog::info("VREditorPapyrusAPI: Exited edit mode via Papyrus");
    }
}

bool Bind(VM* a_vm)
{
    if (!a_vm) {
        spdlog::error("VREditorPapyrusAPI::Bind: VM is null");
        return false;
    }

    constexpr auto scriptName = "VREditorApi"sv;

    a_vm->RegisterFunction("IsInEditMode"sv, scriptName, IsInEditMode);
    a_vm->RegisterFunction("EnterEditMode"sv, scriptName, EnterEditMode);
    a_vm->RegisterFunction("ExitEditMode"sv, scriptName, ExitEditMode);

    spdlog::info("VREditorPapyrusAPI: Registered native functions for '{}'", scriptName);
    return true;
}

} // namespace VREditorPapyrus
} // namespace API
