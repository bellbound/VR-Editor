#pragma once

#include "AddedObjectsParser.h"
#include "ChangedObjectRegistry.h"
#include <unordered_map>
#include <vector>

namespace Persistence {

// AddedObjectsExporter: Exports created objects to per-cell INI files
//
// Purpose:
// - Called during save game to persist created objects to INI files
// - Groups entries by cell into separate INI files
// - Merges with existing INI entries (updating duplicates)
//
// Integration:
// - SaveGameDataManager::OnSave() calls ExportPendingCreatedObjects()
// - ChangedObjectRegistry tracks which objects have wasCreated=true
//
// File Format:
// - One INI file per cell: VREditor_{CellIdentifier}_AddedObjects.ini
// - Files placed in Data folder
// - Format: [AddedObjects] section with base form and transform data
//
// Difference from BOS Exporter:
// - BOS exports repositioned EXISTING references to _SWAP.ini
// - This exports CREATED objects to _AddedObjects.ini
class AddedObjectsExporter {
public:
    static AddedObjectsExporter* GetSingleton();

    // Export all pending created objects to INI files
    // Called during save game
    // Returns number of entries exported
    size_t ExportPendingCreatedObjects();

    // Export a specific set of created object entries
    // Returns number of entries exported
    size_t ExportEntries(const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries);

    // Convert a reference to an AddedObjectEntry
    // Handles rotation extraction and metadata population
    static AddedObjectEntry ReferenceToEntry(RE::TESObjectREFR* ref, RE::FormID baseFormId);

    // Convert a stored NiTransform to an AddedObjectEntry
    // Used when reference is not loaded (cell unloaded)
    static AddedObjectEntry TransformToEntry(const RE::NiTransform& transform, const std::string& baseFormKey);

    // Populate metadata fields for an entry by looking up the base form
    static void PopulateEntryMetadata(AddedObjectEntry& entry);

private:
    AddedObjectsExporter() = default;
    ~AddedObjectsExporter() = default;
    AddedObjectsExporter(const AddedObjectsExporter&) = delete;
    AddedObjectsExporter& operator=(const AddedObjectsExporter&) = delete;

    // Group entries by cell FormKey
    // Returns map of cellFormKey -> (cellEditorId, vector of entries)
    std::unordered_map<std::string, std::pair<std::string, std::vector<AddedObjectEntry>>>
    GroupEntriesByCell(const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries);
};

} // namespace Persistence
