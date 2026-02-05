#pragma once

#include "BaseObjectSwapperParser.h"
#include "ChangedObjectRegistry.h"
#include <unordered_map>
#include <vector>

namespace Persistence {

// BaseObjectSwapperExporter: Exports changed objects to BOS INI files
//
// Purpose:
// - Called during save game to persist transform changes to INI files
// - Groups entries by cell into separate INI files
// - Merges with existing INI entries (updating duplicates)
//
// Session File Strategy:
// BOS locks _SWAP.ini files when loading them on game start, preventing us from
// writing to them during gameplay. To work around this:
// - We write to *_SWAP_session.ini files in Data/VREditor/ (which BOS doesn't know about)
// - On game start, BEFORE BOS loads, ApplyPendingSessionFiles() copies
//   session file contents to the corresponding _SWAP.ini files in Data/
// - This allows our changes to persist even when BOS has files locked
//
// Integration:
// - SaveGameDataManager::OnSave() calls ExportPendingChanges()
// - ChangedObjectRegistry tracks which objects have pending changes
// - Plugin initialization must call BaseObjectSwapperParser::ApplyPendingSessionFiles()
//   BEFORE BOS loads (use SKSEMessagingInterface kDataLoaded or earlier)
//

class BaseObjectSwapperExporter {
public:
    static BaseObjectSwapperExporter* GetSingleton();

    // Export all pending changes to INI files
    // Called during save game
    // Returns number of entries exported
    size_t ExportPendingChanges();

    // Export a specific set of entries (for testing/manual export)
    // Returns number of entries exported
    size_t ExportEntries(const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries);

    // Convert NiTransform to BOS format entry
    // Handles rotation matrix to Euler angles conversion
    // Also populates metadata (editorId, displayName, pluginName) from the reference
    static BOSTransformEntry TransformToBOSEntry(const std::string& formKey,
                                                   const RE::NiTransform& transform,
                                                   bool isDeleted = false);

    // Populate metadata fields for an entry by looking up the reference
    static void PopulateEntryMetadata(BOSTransformEntry& entry);

    // Convert NiMatrix3 rotation to Euler angles (degrees)
    // Returns (pitch, yaw, roll) in degrees, normalized to -180 to +180 range
    static RE::NiPoint3 MatrixToEulerDegrees(const RE::NiMatrix3& matrix);

    // Normalize angle to -180 to +180 degree range
    // Ensures angles stay within BOS's ±360° clamp range
    static float NormalizeAngleDegrees(float angle);
    static RE::NiPoint3 NormalizeAnglesDegrees(const RE::NiPoint3& angles);

private:
    BaseObjectSwapperExporter() = default;
    ~BaseObjectSwapperExporter() = default;
    BaseObjectSwapperExporter(const BaseObjectSwapperExporter&) = delete;
    BaseObjectSwapperExporter& operator=(const BaseObjectSwapperExporter&) = delete;

    // Group entries by cell FormKey
    // Returns map of cellFormKey -> (cellEditorId, entries)
    std::unordered_map<std::string, std::pair<std::string, std::vector<BOSTransformEntry>>>
    GroupEntriesByCell(const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries);
};

} // namespace Persistence
