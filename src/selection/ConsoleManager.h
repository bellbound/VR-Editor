#pragma once

#include <RE/Skyrim.h>

namespace Selection {

/// Bridges VR Editor selection with Skyrim's console system.
/// When enabled, selecting an object in VR Editor also selects it in the console,
/// allowing immediate use of console commands on that object.
class ConsoleManager
{
public:
    /// Optionally sets the console's selected reference (equivalent to "prid <formid>").
    /// Only performs the selection if the "Select in Console" setting is enabled.
    /// Logs the selection with the object's FormKey for debugging.
    /// @param ref - The reference to select in the console (can be nullptr to clear)
    static void MayConsolePrid(RE::TESObjectREFR* ref);
};

} // namespace Selection
