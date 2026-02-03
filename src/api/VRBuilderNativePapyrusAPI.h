#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace API {

/// Papyrus adapter - bridges VRBuilderNative.psc to C++ edit mode functionality.
/// All functions take StaticFunctionTag* as first param (required for global native functions).
namespace VRBuilderNativePapyrus {
    using VM = RE::BSScript::IVirtualMachine;

    /// Toggles edit mode on/off
    void ToggleEditMode(RE::StaticFunctionTag*);

    /// Returns true if currently in edit mode
    bool IsInEditMode(RE::StaticFunctionTag*);

    /// Reset all edits for the player's current cell
    void ResetCurrentCellEdits(RE::StaticFunctionTag*);

    /// Binds native functions to VRBuilderNative script
    bool Bind(VM* a_vm);
}

} // namespace API
