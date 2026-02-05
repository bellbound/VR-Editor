#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <set>

namespace Persistence {

// Shared metadata structure for both BOS and AddedObjects entries
// Used for generating and parsing comment lines above INI entries
//
// Comment Format (pipe-separated, constant field count):
// ; EditorId|DisplayName|MeshPath|FormType
//
// Examples:
// ; Barrel01|Barrel|Clutter\Barrel01.NIF|      (all fields, no formType needed)
// ; |Barrel|Clutter\Barrel01.NIF|              (no editor ID)
// ; Barrel01||Clutter\Barrel01.NIF|            (no display name)
// ; ||Clutter\Barrel01.NIF|                    (only mesh)
// ; |||LIGH                                    (light with no other metadata)
// ; RiftenLight01|||LIGH                       (light with editorId)
//
// Field indices:
// 0 = EditorId
// 1 = DisplayName (no quotes)
// 2 = MeshPath
// 3 = FormType (e.g., "LIGH", "STAT", "ACTI" - only shown when other fields empty)
struct EntryMetadata {
    std::string editorId;
    std::string displayName;
    std::string meshName;
    std::string formTypeName;    // Form type code (e.g., "LIGH", "STAT")

    // Generate comment line in the standard format
    // Returns "; EditorId|DisplayName|MeshPath|FormType"
    std::string ToCommentLine() const;

    // Parse metadata from a comment line
    // Returns true if the line was a valid metadata comment
    // The line should start with "; " and contain pipe-separated fields
    static bool ParseFromComment(std::string_view commentLine, EntryMetadata& outMetadata);

    // Check if all metadata fields are empty (excludes formType from check)
    bool IsEmpty() const;

    // Check if completely empty including formType
    bool IsCompletelyEmpty() const;

    // Merge metadata from another source, only filling in empty fields
    // Useful when combining parsed entries with runtime data
    void MergeFrom(const EntryMetadata& other);
};

// Utility functions for handling formKeyStrings

// Extract plugin name from a formKeyString
// e.g., "0xF81B0~Skyrim.esm" -> "Skyrim.esm"
// Returns empty string if no tilde found (e.g., EditorID-based string)
std::string ExtractPluginFromFormKey(const std::string& formKeyString);

// Collect unique plugin names from a set of formKeyStrings
// Useful for generating the "Plugins referenced" header section
std::set<std::string> CollectPluginNames(const std::vector<std::string>& formKeyStrings);

} // namespace Persistence
