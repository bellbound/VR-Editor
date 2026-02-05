#pragma once

#include "EntryMetadata.h"
#include <RE/N/NiTransform.h>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <filesystem>

namespace Persistence {

// Record flag for "Initially Disabled" - used for deleted references
// See: https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format#Records
// When undeleting, we simply remove the flags() property (no need for flagsC)
constexpr uint32_t INITIALLY_DISABLED_FLAG = 0x00000800;

// Represents a single transform entry in a BOS INI file
// Format: origRefID|posA(x,y,z),rotA(rx,ry,rz),scaleA(s)|100
//
// Comment format (above each entry):
// ; EditorId|DisplayName|MeshPath
struct BOSTransformEntry {
    std::string formKeyString;       // "0x10C0E3~Skyrim.esm"
    RE::NiPoint3 position;           // Absolute position
    RE::NiPoint3 rotation;           // Absolute rotation (degrees)
    float scale = 1.0f;              // Absolute scale

    // Metadata for comments (not serialized to INI line itself)
    // Uses shared EntryMetadata format for parsing/generation
    std::string editorId;            // Editor ID of the reference (e.g., "WhiterunDragonStatue01")
    std::string displayName;         // Display name if available (e.g., "Dragon Statue")
    std::string meshName;            // Mesh/model name (e.g., "architecture/whiterun/wrdragonstatue01.nif")
    std::string formTypeName;        // Form type code (e.g., "LIGH", "STAT") - fallback when other metadata unavailable
    bool isDeleted = false;          // True if this is a "deleted" reference (uses Initially Disabled flag)

    // Convert to BOS INI line format
    std::string ToIniLine() const;

    // Generate a comment line describing this entry
    // Uses unified pipe-separated format: ; EditorId|DisplayName|MeshPath
    std::string ToCommentLine() const;

    // Parse from BOS INI line (returns nullopt if invalid)
    // Also extracts plugin name from formKeyString
    static std::optional<BOSTransformEntry> FromIniLine(std::string_view line);

    // Apply metadata from a parsed comment line
    void ApplyMetadataFromComment(std::string_view commentLine);

    // Get metadata as EntryMetadata struct (for unified handling)
    EntryMetadata GetMetadata() const;

    // Set metadata from EntryMetadata struct
    void SetMetadata(const EntryMetadata& metadata);

    // Get plugin name extracted from formKeyString
    std::string GetPluginName() const;
};

// Represents all entries for a single cell's INI file
struct BOSCellData {
    std::string cellFormKey;         // "0x3C~Skyrim.esm"
    std::string cellEditorId;        // "WhiterunExterior01" (may be empty)
    std::string iniFileName;         // "VREditor_{cellId}_SWAP.ini"
    std::vector<BOSTransformEntry> entries;
};

// BaseObjectSwapperParser: Handles reading/writing BOS _SWAP.ini files
//
// Purpose:
// - Parse existing INI files to detect previously edited references
// - Write/update INI files with new transform data
// - Validate entries before writing
//
// File Format:
// - Files are named: VREditor_{cellId}_SWAP.ini
// - Located in Data folder
// - Contains [Transforms] section with absolute position/rotation entries
class BaseObjectSwapperParser {
public:
    static BaseObjectSwapperParser* GetSingleton();

    // ========== Reading ==========

    // Parse a single INI file and return all transform entries
    // Returns empty vector if file doesn't exist or is empty
    std::vector<BOSTransformEntry> ParseIniFile(const std::filesystem::path& filePath) const;

    // Check if a specific reference exists in an INI file
    bool ContainsReference(const std::filesystem::path& filePath,
                          const std::string& formKeyString) const;

    // Get all VREditor_*_SWAP.ini files in Data folder
    std::vector<std::filesystem::path> FindAllVREditorIniFiles() const;

    // ========== Writing ==========

    // Write entries to an INI file
    // If file exists, merges with existing entries (updating duplicates)
    // Returns true on success
    bool WriteIniFile(const std::filesystem::path& filePath,
                      const std::vector<BOSTransformEntry>& entries) const;

    // Write a cell's data to its INI file
    // Creates the file if it doesn't exist
    bool WriteCellData(const BOSCellData& data) const;

    // ========== Utilities ==========

    // Get the Data folder path (where _SWAP.ini files live for BOS)
    std::filesystem::path GetDataFolderPath() const;

    // Get the VREditor subfolder path (Data/VREditor/)
    // This is where session files and other VREditor-specific files are stored
    // Creates the folder if it doesn't exist
    std::filesystem::path GetVREditorFolderPath() const;

    // Build INI filename for a cell (the main _SWAP.ini file BOS reads)
    // Uses cellEditorId if available, otherwise uses sanitized cellFormKey
    static std::string BuildIniFileName(std::string_view cellEditorId, std::string_view cellFormKey);

    // Build session INI filename for a cell (the _SWAP_session.ini file we write to)
    // This file is not locked by BOS since it doesn't know about it
    static std::string BuildSessionIniFileName(std::string_view cellEditorId, std::string_view cellFormKey);

    // Get the session file path corresponding to a swap file path
    // e.g., "VREditor_Whiterun_SWAP.ini" -> "VREditor_Whiterun_SWAP_session.ini"
    static std::filesystem::path GetSessionFilePath(const std::filesystem::path& swapFilePath);

    // Get the swap file path corresponding to a session file path
    // e.g., "VREditor_Whiterun_SWAP_session.ini" -> "VREditor_Whiterun_SWAP.ini"
    static std::filesystem::path GetSwapFilePath(const std::filesystem::path& sessionFilePath);

    // Apply any pending session files to their corresponding swap files
    // Should be called on game start BEFORE BOS loads its INI files
    // This copies _session.ini contents to _SWAP.ini so BOS sees our changes
    void ApplyPendingSessionFiles() const;

    // Sanitize a string for use in filename
    static std::string SanitizeForFilename(std::string_view input);

    // Format a float with reasonable precision (no trailing zeros)
    static std::string FormatFloat(float value);

    // Parse a property string like "posA(1.0,2.0,3.0),rotA(0,0,90),scaleA(1.5)"
    static bool ParsePropertyString(std::string_view props, BOSTransformEntry& entry);

private:
    BaseObjectSwapperParser() = default;
    ~BaseObjectSwapperParser() = default;
    BaseObjectSwapperParser(const BaseObjectSwapperParser&) = delete;
    BaseObjectSwapperParser& operator=(const BaseObjectSwapperParser&) = delete;
};

} // namespace Persistence
