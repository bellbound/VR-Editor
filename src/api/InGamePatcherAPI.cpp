// =============================================================================
// In-Game Patcher VR - Public API Implementation
// =============================================================================

#define IN_GAME_PATCHER_EXPORTS
#include "InGamePatcherAPI.h"

#include "../EditModeManager.h"
#include "../EditModeStateManager.h"
#include "../selection/SelectionState.h"
#include "../selection/HoverStateManager.h"
#include "../log.h"

#include <RE/Skyrim.h>
#include <mutex>
#include <vector>
#include <algorithm>

// Build number - increment with each release
constexpr uint32_t BUILD_NUMBER = 1;

// =============================================================================
// Callback Management
// =============================================================================

namespace {
    std::mutex g_callbackMutex;
    std::vector<IGP_EditModeCallback> g_editModeCallbacks;
    std::vector<IGP_SelectionCallback> g_selectionCallbacks;
}

// Called by EditModeManager when mode changes
void NotifyEditModeCallbacks(bool isEntering) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    for (auto callback : g_editModeCallbacks) {
        if (callback) {
            callback(isEntering);
        }
    }
}

// Called by SelectionState when selection changes
void NotifySelectionCallbacks(uint32_t formID, bool isSelected) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    for (auto callback : g_selectionCallbacks) {
        if (callback) {
            callback(formID, isSelected);
        }
    }
}

// =============================================================================
// Version & Info
// =============================================================================

IGP_API uint32_t IGP_GetAPIVersion() {
    return IGP_API_VERSION;
}

IGP_API uint32_t IGP_GetBuildNumber() {
    return BUILD_NUMBER;
}

// =============================================================================
// Edit Mode Control
// =============================================================================

IGP_API bool IGP_IsInEditMode() {
    auto* manager = EditModeManager::GetSingleton();
    if (!manager || !manager->IsInitialized()) {
        return false;
    }
    return manager->IsInEditMode();
}

IGP_API bool IGP_EnterEditMode() {
    auto* manager = EditModeManager::GetSingleton();
    if (!manager || !manager->IsInitialized()) {
        SKSE::log::warn("IGP_EnterEditMode: EditModeManager not initialized");
        return false;
    }

    if (manager->IsInEditMode()) {
        SKSE::log::debug("IGP_EnterEditMode: Already in edit mode");
        return false;
    }

    manager->Enter();
    NotifyEditModeCallbacks(true);
    return true;
}

IGP_API bool IGP_ExitEditMode() {
    auto* manager = EditModeManager::GetSingleton();
    if (!manager || !manager->IsInitialized()) {
        SKSE::log::warn("IGP_ExitEditMode: EditModeManager not initialized");
        return false;
    }

    if (!manager->IsInEditMode()) {
        SKSE::log::debug("IGP_ExitEditMode: Not in edit mode");
        return false;
    }

    manager->Exit();
    NotifyEditModeCallbacks(false);
    return true;
}

IGP_API IGP_EditModeState IGP_GetEditModeState() {
    auto* stateManager = EditModeStateManager::GetSingleton();
    if (!stateManager || !stateManager->IsInitialized()) {
        return IGP_State_Unknown;
    }

    // Map internal state to API state
    switch (stateManager->GetState()) {
        case EditModeState::Idle:
            return IGP_State_Idle;
        case EditModeState::Selecting:
            return IGP_State_Selecting;
        case EditModeState::RemotePlacement:
            return IGP_State_RemotePlacement;
        default:
            return IGP_State_Unknown;
    }
}

// =============================================================================
// Selection Queries
// =============================================================================

IGP_API uint32_t IGP_GetSelectionCount() {
    if (!IGP_IsInEditMode()) {
        return 0;
    }

    auto* selection = Selection::SelectionState::GetSingleton();
    if (!selection) {
        return 0;
    }

    return static_cast<uint32_t>(selection->GetSelectionCount());
}

IGP_API uint32_t IGP_GetSelectedFormID(uint32_t index) {
    if (!IGP_IsInEditMode()) {
        return 0;
    }

    auto* selection = Selection::SelectionState::GetSingleton();
    if (!selection) {
        return 0;
    }

    const auto& selectionList = selection->GetSelection();
    if (index >= selectionList.size()) {
        return 0;
    }

    return selectionList[index].formId;
}

IGP_API bool IGP_IsFormSelected(uint32_t formID) {
    if (!IGP_IsInEditMode()) {
        return false;
    }

    auto* selection = Selection::SelectionState::GetSingleton();
    if (!selection) {
        return false;
    }

    return selection->IsSelected(static_cast<RE::FormID>(formID));
}

IGP_API uint32_t IGP_GetHoveredFormID() {
    if (!IGP_IsInEditMode()) {
        return 0;
    }

    auto* stateManager = EditModeStateManager::GetSingleton();
    if (!stateManager || !stateManager->IsSelecting()) {
        return 0;
    }

    auto* hoverManager = Selection::HoverStateManager::GetSingleton();
    if (!hoverManager) {
        return 0;
    }

    auto* hovered = hoverManager->GetHoveredObject();
    if (!hovered) {
        return 0;
    }

    return hovered->GetFormID();
}

// =============================================================================
// Selection Control
// =============================================================================

IGP_API bool IGP_SelectForm(uint32_t formID) {
    if (!IGP_IsInEditMode()) {
        SKSE::log::debug("IGP_SelectForm: Not in edit mode");
        return false;
    }

    auto* selection = Selection::SelectionState::GetSingleton();
    if (!selection) {
        return false;
    }

    // Look up the reference by FormID
    auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(formID));
    if (!form) {
        SKSE::log::debug("IGP_SelectForm: Form {:08X} not found", formID);
        return false;
    }

    auto* ref = form->As<RE::TESObjectREFR>();
    if (!ref) {
        SKSE::log::debug("IGP_SelectForm: Form {:08X} is not a reference", formID);
        return false;
    }

    selection->AddToSelection(ref);
    NotifySelectionCallbacks(formID, true);
    return true;
}

IGP_API bool IGP_DeselectForm(uint32_t formID) {
    if (!IGP_IsInEditMode()) {
        SKSE::log::debug("IGP_DeselectForm: Not in edit mode");
        return false;
    }

    auto* selection = Selection::SelectionState::GetSingleton();
    if (!selection) {
        return false;
    }

    if (!selection->IsSelected(static_cast<RE::FormID>(formID))) {
        return false;
    }

    // Look up the reference by FormID
    auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(formID));
    if (!form) {
        return false;
    }

    auto* ref = form->As<RE::TESObjectREFR>();
    if (!ref) {
        return false;
    }

    selection->RemoveFromSelection(ref);
    NotifySelectionCallbacks(formID, false);
    return true;
}

IGP_API void IGP_ClearSelection() {
    if (!IGP_IsInEditMode()) {
        return;
    }

    auto* selection = Selection::SelectionState::GetSingleton();
    if (selection) {
        selection->ClearAll();
        NotifySelectionCallbacks(0, false);
    }
}

// =============================================================================
// Callbacks
// =============================================================================

IGP_API void IGP_RegisterEditModeCallback(IGP_EditModeCallback callback) {
    if (!callback) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_callbackMutex);

    // Avoid duplicate registration
    auto it = std::find(g_editModeCallbacks.begin(), g_editModeCallbacks.end(), callback);
    if (it == g_editModeCallbacks.end()) {
        g_editModeCallbacks.push_back(callback);
        SKSE::log::debug("IGP: Registered edit mode callback");
    }
}

IGP_API void IGP_UnregisterEditModeCallback(IGP_EditModeCallback callback) {
    if (!callback) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_callbackMutex);

    auto it = std::find(g_editModeCallbacks.begin(), g_editModeCallbacks.end(), callback);
    if (it != g_editModeCallbacks.end()) {
        g_editModeCallbacks.erase(it);
        SKSE::log::debug("IGP: Unregistered edit mode callback");
    }
}

IGP_API void IGP_RegisterSelectionCallback(IGP_SelectionCallback callback) {
    if (!callback) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_callbackMutex);

    auto it = std::find(g_selectionCallbacks.begin(), g_selectionCallbacks.end(), callback);
    if (it == g_selectionCallbacks.end()) {
        g_selectionCallbacks.push_back(callback);
        SKSE::log::debug("IGP: Registered selection callback");
    }
}

IGP_API void IGP_UnregisterSelectionCallback(IGP_SelectionCallback callback) {
    if (!callback) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_callbackMutex);

    auto it = std::find(g_selectionCallbacks.begin(), g_selectionCallbacks.end(), callback);
    if (it != g_selectionCallbacks.end()) {
        g_selectionCallbacks.erase(it);
        SKSE::log::debug("IGP: Unregistered selection callback");
    }
}
