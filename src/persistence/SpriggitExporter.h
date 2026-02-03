#pragma once

#include "ChangedObjectRegistry.h"
#include <RE/T/TESObjectCELL.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

namespace Persistence {

// SpriggitExporter: Exports changed objects to Spriggit-compatible partial YAML files
//
// Purpose:
// - Creates partial YAML files that modders can use to patch their ESP with changes
// - This is a side-effect output for advanced users, not a source of truth
// - Generated on save alongside other export formats
//
// File Layout:
// - VREditor/spriggit-partials/Cells/{Block}/{SubBlock}/{CellEditorId} - {CellFormId}_{Plugin}/RecordData.partial.yaml
// - Block and SubBlock are calculated from cell FormID for interior cells
// - For exterior cells, derived from grid coordinates
//
// YAML Format:
// - Uses Mutagen/Spriggit PlacedObject format
// - Moved objects: Have FormKey set to the reference's FormKey
// - Deleted objects: Have IsDeleted: True and SkyrimMajorRecordFlags: [Deleted]
// - Added objects: Have FormKey: (empty) for new FormID assignment
class SpriggitExporter {
public:
    static SpriggitExporter* GetSingleton();

    // Export all changed objects to Spriggit partial YAML files
    // Called during save game
    // Returns number of entries exported
    size_t ExportAllChanges();

    // Delete Spriggit partials for a specific cell
    // Returns true if a file or directory was removed
    bool DeleteCellExport(RE::TESObjectCELL* cell,
                          const std::string& cellFormKey,
                          const std::string& cellEditorId);

private:
    SpriggitExporter() = default;
    ~SpriggitExporter() = default;
    SpriggitExporter(const SpriggitExporter&) = delete;
    SpriggitExporter& operator=(const SpriggitExporter&) = delete;

    // Data structure to hold entries grouped by cell
    struct CellExportData {
        RE::TESObjectCELL* cell = nullptr;
        std::string cellEditorId;
        std::string cellFormKey;      // e.g., "0165B5:Skyrim.esm"
        int32_t block = 0;
        int32_t subBlock = 0;
        std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>> entries;
    };

    // Group all entries by their parent cell
    std::unordered_map<RE::FormID, CellExportData> GroupEntriesByCell();

    // Calculate Block and SubBlock for a cell
    // For interior cells: based on FormID hash
    // For exterior cells: based on grid coordinates
    static void CalculateBlockSubBlock(RE::TESObjectCELL* cell, int32_t& block, int32_t& subBlock);

    // Build the directory path for a cell's partial YAML
    // Returns: VREditor/spriggit-partials/Cells/{Block}/{SubBlock}/{CellEditorId} - {CellFormId}_{Plugin}/
    std::filesystem::path BuildCellDirectoryPath(const CellExportData& cellData) const;

    // Get the base VREditor folder path (Data/VREditor/)
    std::filesystem::path GetVREditorFolderPath() const;

    // Write YAML file for a cell's entries
    bool WritePartialYaml(const std::filesystem::path& dirPath, const CellExportData& cellData) const;

    // Generate YAML content for a moved/modified reference
    std::string GenerateMovedEntryYaml(const std::string& formKey,
                                        const ChangedObjectRuntimeData* data) const;

    // Generate YAML content for a deleted reference
    std::string GenerateDeletedEntryYaml(const std::string& formKey,
                                          const ChangedObjectRuntimeData* data) const;

    // Generate YAML content for an added (new) reference
    std::string GenerateAddedEntryYaml(const std::string& formKey,
                                        const ChangedObjectRuntimeData* data) const;

    // Convert FormKey string (0x123ABC~Plugin.esm) to Spriggit format (123ABC:Plugin.esm)
    static std::string ToSpriggitFormKey(const std::string& formKey);

    // Format a float for YAML output
    static std::string FormatFloat(float value);

    // Sanitize a string for use in directory name
    static std::string SanitizeForDirName(const std::string& input);
};

} // namespace Persistence
