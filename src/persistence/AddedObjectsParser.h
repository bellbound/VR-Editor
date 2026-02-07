#pragma once

#include "EntryMetadata.h"
#include <RE/N/NiTransform.h>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace Persistence {

// Represents a single object entry in an AddedObjects INI file
// Format: baseForm|posA(x,y,z),rotA(rx,ry,rz),scaleA(s)
//
// Comment format (above each entry):
// ; EditorId|DisplayName|MeshPath
struct AddedObjectEntry {
    std::string baseFormString;      // EditorID or FormKey (0xID~Plugin) of base object
    RE::NiPoint3 position;           // Absolute position
    RE::NiPoint3 rotation;           // Absolute rotation (degrees)
    float scale = 1.0f;              // Absolute scale

    // Metadata for comments (not serialized to INI line itself)
    // Uses shared EntryMetadata format for parsing/generation
    std::string editorId;            // Editor ID of the base object
    std::string displayName;         // Display name if available
    std::string meshName;            // Mesh/model name
    std::string formTypeName;        // Form type code (e.g., "LIGH", "STAT") - fallback when other metadata unavailable

    // Convert to INI line format
    std::string ToIniLine() const;

    // Generate a comment line describing this entry
    // Uses unified pipe-separated format: ; EditorId|DisplayName|MeshPath
    std::string ToCommentLine() const;

    // Parse from INI line (returns nullopt if invalid)
    static std::optional<AddedObjectEntry> FromIniLine(std::string_view line);

    // Apply metadata from a parsed comment line
    void ApplyMetadataFromComment(std::string_view commentLine);

    // Get metadata as EntryMetadata struct (for unified handling)
    EntryMetadata GetMetadata() const;

    // Set metadata from EntryMetadata struct
    void SetMetadata(const EntryMetadata& metadata);

    // Get plugin name extracted from baseFormString (if it's a FormKey)
    std::string GetPluginName() const;
};

// Represents all data for a single cell's AddedObjects INI file
struct AddedObjectsFileData {
    std::string cellFormKey;         // Cell FormKey this file is for
    std::string cellEditorId;        // Cell Editor ID if available
    std::string iniFileName;         // Filename of the INI file
    std::vector<AddedObjectEntry> entries;
};

// Data for writing a cell section (used for consolidated mode)
struct AddedObjectsCellSection {
    std::string cellFormKey;          // Cell's FormKey
    std::string cellEditorId;         // Cell's EditorID if available
    std::vector<AddedObjectEntry> entries;
};

// AddedObjectsParser: Handles reading/writing _AddedObjects.ini files
//
// Purpose:
// - Parse existing INI files to find objects that need to be spawned
// - Write INI files with new objects created by the mod
//
// File Location:
// - All AddedObjects files are stored in Data/VREditor/ subfolder
// - One file per cell: Data/VREditor/VREditor_{CellIdentifier}_AddedObjects.ini
// - CellIdentifier is EditorID (preferred) or FormKey
// - Contains [AddedObjects] section with base form and transform data
// - Header contains Cell FormKey comment for reliable lookup
//
// Difference from BOS (_SWAP.ini):
// - _SWAP.ini: Repositions EXISTING world references (handled by Base Object Swapper)
//   Located in Data/ folder for BOS to read
// - _AddedObjects.ini: SPAWNS NEW objects from base forms (handled by this mod)
//   Located in Data/VREditor/ folder
class AddedObjectsParser {
public:
    static AddedObjectsParser* GetSingleton();

    // ========== Reading ==========

    // Parse a single INI file and return file data including cell info and entries
    // Returns empty data if file doesn't exist or is empty
    AddedObjectsFileData ParseIniFile(const std::filesystem::path& filePath) const;

    // Get all VREditor_*_AddedObjects.ini files in Data folder
    std::vector<std::filesystem::path> FindAllAddedObjectsIniFiles() const;

    // ========== Writing ==========

    // Write entries to an INI file for a specific cell
    // If file exists, merges with existing entries (updating duplicates by position)
    // Returns true on success
    bool WriteIniFile(const std::filesystem::path& filePath,
                      const std::string& cellFormKey,
                      const std::string& cellEditorId,
                      const std::vector<AddedObjectEntry>& entries) const;

    // Write file data to its INI file
    // Creates the file if it doesn't exist
    bool WriteFileData(const AddedObjectsFileData& data) const;

    // Write multiple cells to a single consolidated INI file
    // Used when per-cell mode is disabled
    // File path should be Data/SKSE/Plugins/VREditor/VREditor_AddedObjects.ini
    bool WriteConsolidatedIniFile(const std::filesystem::path& filePath,
                                   const std::vector<AddedObjectsCellSection>& cellSections) const;

    // ========== Utilities ==========

    // Get the VREditor folder path (Data/VREditor/)
    // This is where AddedObjects files are stored
    // Creates the folder if it doesn't exist
    std::filesystem::path GetVREditorFolderPath() const;

    // Build INI filename for a cell
    // Uses EditorID if available, otherwise FormKey
    static std::string BuildIniFileName(RE::TESObjectCELL* cell);
    static std::string BuildIniFileName(const std::string& cellEditorId, const std::string& cellFormKey);

    // Sanitize a string for use in filename
    static std::string SanitizeForFilename(std::string_view input);

    // Format a float with reasonable precision (no trailing zeros)
    static std::string FormatFloat(float value);

    // Parse a property string like "posA(1.0,2.0,3.0),rotA(0,0,90),scaleA(1.5)"
    static bool ParsePropertyString(std::string_view props, AddedObjectEntry& entry);

    // Resolve a base form string to a TESForm
    // Tries EditorID first, then FormKey
    static RE::TESForm* ResolveBaseForm(const std::string& baseFormString);

private:
    AddedObjectsParser() = default;
    ~AddedObjectsParser() = default;
    AddedObjectsParser(const AddedObjectsParser&) = delete;
    AddedObjectsParser& operator=(const AddedObjectsParser&) = delete;

    // Extract cell FormKey from INI header comment
    // Looks for "; Cell FormKey: 0xID~Plugin" line
    std::string ExtractCellFormKeyFromHeader(const std::filesystem::path& filePath) const;
};

} // namespace Persistence
