#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace API {

/// Papyrus adapter - bridges VREditorApi.psc to C++ edit mode functionality.
/// All functions take StaticFunctionTag* as first param (required for global native functions).
namespace VREditorPapyrus {
    using VM = RE::BSScript::IVirtualMachine;

    /// Returns true if currently in edit mode
    bool IsInEditMode(RE::StaticFunctionTag*);

    /// Enters edit mode (if not already in it)
    void EnterEditMode(RE::StaticFunctionTag*);

    /// Exits edit mode (if currently in it)
    void ExitEditMode(RE::StaticFunctionTag*);

    /// Binds native functions to VREditorApi script
    bool Bind(VM* a_vm);
}

} // namespace API
