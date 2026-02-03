#pragma once

// =============================================================================
// In-Game Patcher VR - Public API
// =============================================================================
// This header is designed to be copied to other SKSE projects.
// Provides ABI-safe DLL exports for interacting with In-Game Patcher VR.
//
// Usage from consuming mod:
//   1. Copy this header to your project
//   2. Load the DLL at runtime via LoadLibrary/GetProcAddress
//   3. Check API version for compatibility
//   4. Call functions as needed
//
// Example:
//   auto hDLL = LoadLibraryA("InGamePatcherVR");
//   if (hDLL) {
//       auto GetAPIVersion = (IGP_GetAPIVersionFn)GetProcAddress(hDLL, "IGP_GetAPIVersion");
//       if (GetAPIVersion && GetAPIVersion() >= 1) {
//           auto IsInEditMode = (IGP_IsInEditModeFn)GetProcAddress(hDLL, "IGP_IsInEditMode");
//           if (IsInEditMode && IsInEditMode()) {
//               // Currently in edit mode
//           }
//       }
//   }
//
// Thread Safety:
//   All API calls should be made from the game's main thread unless noted.
//   Callback functions are invoked on the main thread.
//
// ABI Stability:
//   - Uses extern "C" to prevent name mangling
//   - Uses only C-compatible types (no std:: types)
//   - FormID is uint32_t as used by Skyrim
//   - Version checking allows graceful handling of API changes
// =============================================================================

#include <cstdint>

// =============================================================================
// Build Configuration
// =============================================================================

#ifdef IN_GAME_PATCHER_EXPORTS
    #define IGP_API extern "C" __declspec(dllexport)
#else
    #define IGP_API extern "C" __declspec(dllimport)
#endif

// =============================================================================
// API Version
// =============================================================================

// Current API version - increment when adding new functions
constexpr uint32_t IGP_API_VERSION = 1;

// =============================================================================
// Types
// =============================================================================

// Edit mode states - matches EditModeState enum
enum IGP_EditModeState : uint32_t {
    IGP_State_Idle = 0,              // Not doing anything
    IGP_State_Selecting = 1,         // Ray-based selection mode
    IGP_State_RemotePlacement = 2,   // Object being positioned
    IGP_State_Unknown = 0xFFFFFFFF
};

// Callback for edit mode changes
// isEntering: true when entering edit mode, false when exiting
typedef void (*IGP_EditModeCallback)(bool isEntering);

// Callback for selection changes
// formID: the FormID of the selected/deselected object (0 if selection cleared)
// isSelected: true if object was selected, false if deselected
typedef void (*IGP_SelectionCallback)(uint32_t formID, bool isSelected);

// =============================================================================
// Function Pointer Types (for GetProcAddress usage)
// =============================================================================

typedef uint32_t (*IGP_GetAPIVersionFn)();
typedef uint32_t (*IGP_GetBuildNumberFn)();

// Edit Mode Control
typedef bool (*IGP_IsInEditModeFn)();
typedef bool (*IGP_EnterEditModeFn)();
typedef bool (*IGP_ExitEditModeFn)();
typedef IGP_EditModeState (*IGP_GetEditModeStateFn)();

// Selection Queries
typedef uint32_t (*IGP_GetSelectionCountFn)();
typedef uint32_t (*IGP_GetSelectedFormIDFn)(uint32_t index);
typedef bool (*IGP_IsFormSelectedFn)(uint32_t formID);
typedef uint32_t (*IGP_GetHoveredFormIDFn)();

// Selection Control
typedef bool (*IGP_SelectFormFn)(uint32_t formID);
typedef bool (*IGP_DeselectFormFn)(uint32_t formID);
typedef void (*IGP_ClearSelectionFn)();

// Callbacks
typedef void (*IGP_RegisterEditModeCallbackFn)(IGP_EditModeCallback callback);
typedef void (*IGP_UnregisterEditModeCallbackFn)(IGP_EditModeCallback callback);
typedef void (*IGP_RegisterSelectionCallbackFn)(IGP_SelectionCallback callback);
typedef void (*IGP_UnregisterSelectionCallbackFn)(IGP_SelectionCallback callback);

// =============================================================================
// API Function Declarations
// =============================================================================

// -----------------------------------------------------------------------------
// Version & Info
// -----------------------------------------------------------------------------

// Returns the API version number
// Use this to check compatibility before calling other functions
IGP_API uint32_t IGP_GetAPIVersion();

// Returns the build number of the In-Game Patcher
// Useful for debugging and reporting issues
IGP_API uint32_t IGP_GetBuildNumber();

// -----------------------------------------------------------------------------
// Edit Mode Control
// -----------------------------------------------------------------------------

// Returns true if currently in edit mode
IGP_API bool IGP_IsInEditMode();

// Enter edit mode
// Returns true if successfully entered, false if already in edit mode or failed
// Note: This will disable HIGGS grip/trigger and take over VR input
IGP_API bool IGP_EnterEditMode();

// Exit edit mode
// Returns true if successfully exited, false if not in edit mode
// Note: This will restore HIGGS settings and normal VR input
IGP_API bool IGP_ExitEditMode();

// Get the current edit mode state
// Returns IGP_State_Unknown if not initialized
IGP_API IGP_EditModeState IGP_GetEditModeState();

// -----------------------------------------------------------------------------
// Selection Queries
// -----------------------------------------------------------------------------

// Get the number of currently selected objects
// Returns 0 if not in edit mode or nothing selected
IGP_API uint32_t IGP_GetSelectionCount();

// Get the FormID of a selected object by index
// Returns 0 if index is out of range or not in edit mode
// index: 0-based index into selection list
IGP_API uint32_t IGP_GetSelectedFormID(uint32_t index);

// Check if a specific FormID is currently selected
// Returns false if not in edit mode
IGP_API bool IGP_IsFormSelected(uint32_t formID);

// Get the FormID of the currently hovered object (if any)
// Returns 0 if nothing is hovered or not in selection mode
IGP_API uint32_t IGP_GetHoveredFormID();

// -----------------------------------------------------------------------------
// Selection Control
// -----------------------------------------------------------------------------

// Add a form to the selection by FormID
// Returns true if the form was found and selected
// Returns false if form not found, invalid, or not in edit mode
IGP_API bool IGP_SelectForm(uint32_t formID);

// Remove a form from the selection by FormID
// Returns true if the form was deselected
// Returns false if form wasn't selected or not in edit mode
IGP_API bool IGP_DeselectForm(uint32_t formID);

// Clear all selected objects
// Safe to call even if nothing is selected
IGP_API void IGP_ClearSelection();

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------

// Register a callback for edit mode enter/exit events
// The callback will be invoked on the main game thread
// Multiple callbacks can be registered
IGP_API void IGP_RegisterEditModeCallback(IGP_EditModeCallback callback);

// Unregister a previously registered edit mode callback
IGP_API void IGP_UnregisterEditModeCallback(IGP_EditModeCallback callback);

// Register a callback for selection change events
// The callback will be invoked on the main game thread
IGP_API void IGP_RegisterSelectionCallback(IGP_SelectionCallback callback);

// Unregister a previously registered selection callback
IGP_API void IGP_UnregisterSelectionCallback(IGP_SelectionCallback callback);

// =============================================================================
// Helper for Loading API at Runtime
// =============================================================================

#ifndef IN_GAME_PATCHER_EXPORTS

#include <windows.h>

namespace InGamePatcherAPI {

    // Function pointers - populated by FindFunctions()
    inline IGP_GetAPIVersionFn GetAPIVersion = nullptr;
    inline IGP_GetBuildNumberFn GetBuildNumber = nullptr;
    inline IGP_IsInEditModeFn IsInEditMode = nullptr;
    inline IGP_EnterEditModeFn EnterEditMode = nullptr;
    inline IGP_ExitEditModeFn ExitEditMode = nullptr;
    inline IGP_GetEditModeStateFn GetEditModeState = nullptr;
    inline IGP_GetSelectionCountFn GetSelectionCount = nullptr;
    inline IGP_GetSelectedFormIDFn GetSelectedFormID = nullptr;
    inline IGP_IsFormSelectedFn IsFormSelected = nullptr;
    inline IGP_GetHoveredFormIDFn GetHoveredFormID = nullptr;
    inline IGP_SelectFormFn SelectForm = nullptr;
    inline IGP_DeselectFormFn DeselectForm = nullptr;
    inline IGP_ClearSelectionFn ClearSelection = nullptr;
    inline IGP_RegisterEditModeCallbackFn RegisterEditModeCallback = nullptr;
    inline IGP_UnregisterEditModeCallbackFn UnregisterEditModeCallback = nullptr;
    inline IGP_RegisterSelectionCallbackFn RegisterSelectionCallback = nullptr;
    inline IGP_UnregisterSelectionCallbackFn UnregisterSelectionCallback = nullptr;

    // Attempts to load the In-Game Patcher DLL and populate function pointers
    // Returns true if DLL was found and API version is compatible
    // Returns false if DLL not found or API version mismatch
    inline bool FindFunctions() {
        auto hDLL = LoadLibraryA("InGamePatcherVR");
        if (!hDLL) {
            return false;
        }

        // Get version first to check compatibility
        GetAPIVersion = (IGP_GetAPIVersionFn)GetProcAddress(hDLL, "IGP_GetAPIVersion");
        if (!GetAPIVersion || GetAPIVersion() < IGP_API_VERSION) {
            return false;
        }

        // Load all other functions
        GetBuildNumber = (IGP_GetBuildNumberFn)GetProcAddress(hDLL, "IGP_GetBuildNumber");
        IsInEditMode = (IGP_IsInEditModeFn)GetProcAddress(hDLL, "IGP_IsInEditMode");
        EnterEditMode = (IGP_EnterEditModeFn)GetProcAddress(hDLL, "IGP_EnterEditMode");
        ExitEditMode = (IGP_ExitEditModeFn)GetProcAddress(hDLL, "IGP_ExitEditMode");
        GetEditModeState = (IGP_GetEditModeStateFn)GetProcAddress(hDLL, "IGP_GetEditModeState");
        GetSelectionCount = (IGP_GetSelectionCountFn)GetProcAddress(hDLL, "IGP_GetSelectionCount");
        GetSelectedFormID = (IGP_GetSelectedFormIDFn)GetProcAddress(hDLL, "IGP_GetSelectedFormID");
        IsFormSelected = (IGP_IsFormSelectedFn)GetProcAddress(hDLL, "IGP_IsFormSelected");
        GetHoveredFormID = (IGP_GetHoveredFormIDFn)GetProcAddress(hDLL, "IGP_GetHoveredFormID");
        SelectForm = (IGP_SelectFormFn)GetProcAddress(hDLL, "IGP_SelectForm");
        DeselectForm = (IGP_DeselectFormFn)GetProcAddress(hDLL, "IGP_DeselectForm");
        ClearSelection = (IGP_ClearSelectionFn)GetProcAddress(hDLL, "IGP_ClearSelection");
        RegisterEditModeCallback = (IGP_RegisterEditModeCallbackFn)GetProcAddress(hDLL, "IGP_RegisterEditModeCallback");
        UnregisterEditModeCallback = (IGP_UnregisterEditModeCallbackFn)GetProcAddress(hDLL, "IGP_UnregisterEditModeCallback");
        RegisterSelectionCallback = (IGP_RegisterSelectionCallbackFn)GetProcAddress(hDLL, "IGP_RegisterSelectionCallback");
        UnregisterSelectionCallback = (IGP_UnregisterSelectionCallbackFn)GetProcAddress(hDLL, "IGP_UnregisterSelectionCallback");

        return true;
    }

} // namespace InGamePatcherAPI

#endif // !IN_GAME_PATCHER_EXPORTS
