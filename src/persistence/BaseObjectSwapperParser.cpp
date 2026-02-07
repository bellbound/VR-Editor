#include "BaseObjectSwapperParser.h"
#include "../log.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <stdexcept>
#include <set>

namespace Persistence {

// ============================================================================
// BOSTransformEntry
// ============================================================================

std::string BOSTransformEntry::ToIniLine() const
{
    std::ostringstream ss;

    // Format: formKey|posA(x,y,z),rotA(rx,ry,rz),scaleA(s),flags(0x...)|100
    ss << formKeyString << "|";

    // Position
    ss << "posA(" << BaseObjectSwapperParser::FormatFloat(position.x) << ","
       << BaseObjectSwapperParser::FormatFloat(position.y) << ","
       << BaseObjectSwapperParser::FormatFloat(position.z) << ")";

    // Rotation
    ss << ",rotA(" << BaseObjectSwapperParser::FormatFloat(rotation.x) << ","
       << BaseObjectSwapperParser::FormatFloat(rotation.y) << ","
       << BaseObjectSwapperParser::FormatFloat(rotation.z) << ")";

    // Scale (only if not 1.0)
    if (std::abs(scale - 1.0f) > 0.0001f) {
        ss << ",scaleA(" << BaseObjectSwapperParser::FormatFloat(scale) << ")";
    }

    // Initially Disabled flag for deleted references
    // When undeleting, we remove flags() entirely (no need for flagsC to clear)
    if (isDeleted) {
        ss << ",flags(0x" << std::hex << std::setfill('0') << std::setw(8)
           << INITIALLY_DISABLED_FLAG << ")";
    }

    // Chance is always 100
    ss << "|100";

    return ss.str();
}

std::string BOSTransformEntry::ToCommentLine() const
{
    // Use unified pipe-separated format: ; EditorId|DisplayName|MeshPath
    EntryMetadata metadata;
    metadata.editorId = editorId;
    metadata.displayName = displayName;
    metadata.meshName = meshName;
    return metadata.ToCommentLine();
}

void BOSTransformEntry::ApplyMetadataFromComment(std::string_view commentLine)
{
    EntryMetadata metadata;
    if (EntryMetadata::ParseFromComment(commentLine, metadata)) {
        // Only fill in empty fields (don't overwrite existing data)
        if (editorId.empty()) editorId = metadata.editorId;
        if (displayName.empty()) displayName = metadata.displayName;
        if (meshName.empty()) meshName = metadata.meshName;
        if (formTypeName.empty()) formTypeName = metadata.formTypeName;
    }
}

EntryMetadata BOSTransformEntry::GetMetadata() const
{
    EntryMetadata metadata;
    metadata.editorId = editorId;
    metadata.displayName = displayName;
    metadata.meshName = meshName;
    metadata.formTypeName = formTypeName;
    return metadata;
}

void BOSTransformEntry::SetMetadata(const EntryMetadata& metadata)
{
    editorId = metadata.editorId;
    displayName = metadata.displayName;
    meshName = metadata.meshName;
    formTypeName = metadata.formTypeName;
}

std::string BOSTransformEntry::GetPluginName() const
{
    return ExtractPluginFromFormKey(formKeyString);
}

std::optional<BOSTransformEntry> BOSTransformEntry::FromIniLine(std::string_view line)
{
    // Skip empty lines and comments
    if (line.empty() || line[0] == ';' || line[0] == '#') {
        return std::nullopt;
    }

    // Skip section headers
    if (line[0] == '[') {
        return std::nullopt;
    }

    // Split by '|'
    std::string lineStr(line);
    std::vector<std::string> parts;
    std::istringstream ss(lineStr);
    std::string part;
    while (std::getline(ss, part, '|')) {
        parts.push_back(part);
    }

    // Need at least formKey|properties (chance is optional)
    if (parts.size() < 2) {
        return std::nullopt;
    }

    BOSTransformEntry entry;
    entry.formKeyString = parts[0];

    // Trim whitespace from formKey
    entry.formKeyString.erase(0, entry.formKeyString.find_first_not_of(" \t"));
    entry.formKeyString.erase(entry.formKeyString.find_last_not_of(" \t") + 1);

    // Parse properties
    if (!BaseObjectSwapperParser::ParsePropertyString(parts[1], entry)) {
        return std::nullopt;
    }

    return entry;
}

// ============================================================================
// BaseObjectSwapperParser
// ============================================================================

BaseObjectSwapperParser* BaseObjectSwapperParser::GetSingleton()
{
    static BaseObjectSwapperParser instance;
    return &instance;
}

std::vector<BOSTransformEntry> BaseObjectSwapperParser::ParseIniFile(
    const std::filesystem::path& filePath) const
{
    std::vector<BOSTransformEntry> entries;

    if (!std::filesystem::exists(filePath)) {
        return entries;
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        spdlog::warn("BaseObjectSwapperParser: Failed to open {}", filePath.string());
        return entries;
    }

    std::string line;
    std::string lastComment;  // Track the comment line preceding each entry
    bool inTransformsSection = false;

    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) {
            // Empty line resets comment tracking
            lastComment.clear();
            continue;
        }

        // Track comments - they may contain metadata for the next entry
        if (line[0] == ';' || line[0] == '#') {
            // Check if this looks like a metadata comment (contains pipes)
            if (line.find('|') != std::string::npos) {
                lastComment = line;
            }
            continue;
        }

        // Check for section header
        if (line[0] == '[') {
            // Check if this is [Transforms] section (with optional location filter)
            inTransformsSection = (line.find("[Transforms") == 0);
            lastComment.clear();
            continue;
        }

        // Only parse lines in [Transforms] section
        if (inTransformsSection) {
            auto entry = BOSTransformEntry::FromIniLine(line);
            if (entry) {
                // Apply metadata from the preceding comment line if present
                if (!lastComment.empty()) {
                    entry->ApplyMetadataFromComment(lastComment);
                }
                entries.push_back(std::move(*entry));
            }
            lastComment.clear();
        }
    }

    spdlog::trace("BaseObjectSwapperParser: Parsed {} entries from {}",
        entries.size(), filePath.string());

    return entries;
}

bool BaseObjectSwapperParser::ContainsReference(const std::filesystem::path& filePath,
                                                 const std::string& formKeyString) const
{
    auto entries = ParseIniFile(filePath);
    return std::any_of(entries.begin(), entries.end(),
        [&formKeyString](const BOSTransformEntry& e) {
            return e.formKeyString == formKeyString;
        });
}

std::vector<std::filesystem::path> BaseObjectSwapperParser::FindAllVREditorIniFiles() const
{
    std::vector<std::filesystem::path> files;
    auto dataPath = GetDataFolderPath();

    if (!std::filesystem::exists(dataPath)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dataPath)) {
        if (entry.is_regular_file()) {
            auto filename = entry.path().filename().string();
            // Check for VREditor_*_SWAP.ini pattern
            if (filename.starts_with("VREditor_") && filename.ends_with("_SWAP.ini")) {
                files.push_back(entry.path());
            }
        }
    }

    return files;
}

bool BaseObjectSwapperParser::WriteIniFile(const std::filesystem::path& filePath,
                                            const std::vector<BOSTransformEntry>& entries) const
{
    if (entries.empty()) {
        return true;  // Nothing to write
    }

    auto latestFilePath = GetLatestFilePath(filePath);

    // Latest files are in the same Data/ folder, no need to create directories

    // Read existing entries and merge
    // Priority: latest file first (our pending changes), then swap file (BOS baseline)
    std::unordered_map<std::string, BOSTransformEntry> mergedEntries;

    // Load existing entries - prefer latest file if it exists, otherwise use swap file
    {
        std::vector<BOSTransformEntry> existingEntries;
        if (std::filesystem::exists(latestFilePath)) {
            // Latest file exists - use it (contains our accumulated changes)
            existingEntries = ParseIniFile(latestFilePath);
            spdlog::trace("BaseObjectSwapperParser: Merging with existing latest file {}",
                latestFilePath.filename().string());
        } else if (std::filesystem::exists(filePath)) {
            // No latest file yet - start from the swap file (BOS baseline)
            existingEntries = ParseIniFile(filePath);
            spdlog::trace("BaseObjectSwapperParser: Starting from swap file {}",
                filePath.filename().string());
        }

        for (auto& entry : existingEntries) {
            mergedEntries[entry.formKeyString] = std::move(entry);
        }
    }

    // Smart merge: add/update with new entries, preserving existing metadata when new is empty
    for (const auto& entry : entries) {
        auto it = mergedEntries.find(entry.formKeyString);
        if (it != mergedEntries.end()) {
            // Entry exists - merge metadata (preserve existing if new is empty)
            BOSTransformEntry merged = entry;
            EntryMetadata existingMeta = it->second.GetMetadata();
            EntryMetadata newMeta = merged.GetMetadata();
            newMeta.MergeFrom(existingMeta);  // Fill empty fields from existing
            merged.SetMetadata(newMeta);
            mergedEntries[entry.formKeyString] = std::move(merged);
        } else {
            // New entry
            mergedEntries[entry.formKeyString] = entry;
        }
    }

    // Collect unique plugin names from formKeyStrings and separate moved vs deleted entries
    std::set<std::string> pluginNames;
    std::vector<const BOSTransformEntry*> movedEntries;
    std::vector<const BOSTransformEntry*> deletedEntries;

    for (const auto& [key, entry] : mergedEntries) {
        // Extract plugin name directly from formKeyString
        std::string plugin = entry.GetPluginName();
        if (!plugin.empty()) {
            pluginNames.insert(plugin);
        }

        if (entry.isDeleted) {
            deletedEntries.push_back(&entry);
        } else {
            movedEntries.push_back(&entry);
        }
    }

    // Write to the LATEST file (not the swap file) to avoid BOS file lock
    // BOS locks _SWAP.ini files when loading, but doesn't know about _latest.ini
    // On next game start, ApplyPendingLatestFiles() will copy this to the swap file
    std::ofstream file(latestFilePath, std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("BaseObjectSwapperParser: Failed to open {} for writing",
            latestFilePath.string());
        return false;
    }

    // Write header comment
    file << "; ============================================================\n";
    file << "; VR Editor Transform Data\n";
    file << "; Auto-generated by In-Game Patcher VR\n";
    file << "; ============================================================\n";
    file << ";\n";

    // Write involved plugins
    if (!pluginNames.empty()) {
        file << "; Plugins referenced in this file:\n";
        for (const auto& plugin : pluginNames) {
            file << ";   - " << plugin << "\n";
        }
        file << ";\n";
    }

    // Write [Transforms] section for moved (non-deleted) entries
    file << "[Transforms]\n";
    file << "; Repositioned objects (" << movedEntries.size() << " entries)\n";
    file << "\n";

    for (const auto* entry : movedEntries) {
        // Always write comment line for consistency (enables metadata preservation on merge)
        file << entry->ToCommentLine() << "\n";
        file << entry->ToIniLine() << "\n";
        file << "\n";
    }

    // Write deleted entries section if any
    if (!deletedEntries.empty()) {
        file << "; ============================================================\n";
        file << "; DELETED REFERENCES (" << deletedEntries.size() << " entries)\n";
        file << "; These objects have the Initially Disabled flag (0x00000800) set\n";
        file << "; To restore: remove the flags() property from the entry\n";
        file << "; ============================================================\n";
        file << "\n";

        for (const auto* entry : deletedEntries) {
            // Always write comment line for consistency (enables metadata preservation on merge)
            file << entry->ToCommentLine() << "\n";
            file << entry->ToIniLine() << "\n";
            file << "\n";
        }
    }

    // Ensure all data is flushed to disk
    file.flush();
    if (file.fail()) {
        spdlog::error("BaseObjectSwapperParser: Failed to write data to {}", latestFilePath.string());
        return false;
    }

    spdlog::info("BaseObjectSwapperParser: Wrote {} entries ({} moved, {} deleted) to latest file {}",
        mergedEntries.size(), movedEntries.size(), deletedEntries.size(), latestFilePath.filename().string());

    return true;
}

bool BaseObjectSwapperParser::WriteCellData(const BOSCellData& data) const
{
    auto dataPath = GetDataFolderPath();
    auto filePath = dataPath / data.iniFileName;

    return WriteIniFile(filePath, data.entries);
}

void BaseObjectSwapperParser::WriteFileHeader(std::ostream& out)
{
    out << "; ============================================================\n";
    out << "; VR Editor Transform Data\n";
    out << "; Auto-generated by VR Editor\n";
    out << "; ============================================================\n";
    out << "\n";
}

void BaseObjectSwapperParser::WriteCellSection(std::ostream& out,
                                                const std::string& cellName,
                                                const std::string& cellFormKey,
                                                const std::vector<const BOSTransformEntry*>& movedEntries,
                                                const std::vector<const BOSTransformEntry*>& deletedEntries,
                                                const std::set<std::string>& plugins)
{
    size_t totalChanges = movedEntries.size() + deletedEntries.size();

    // Cell section header
    out << "; ==================== " << cellName << " ====================\n";
    out << "; Cell: " << cellName << "\n";
    out << "; Cell FormKey: " << cellFormKey << "\n";
    out << "; Changes Made: " << totalChanges << "\n";

    // Plugins referenced in this cell
    if (!plugins.empty()) {
        out << "; Plugins referenced in this cell's changes:\n";
        for (const auto& plugin : plugins) {
            out << ";   - " << plugin << "\n";
        }
    }
    out << "\n";

    // Write moved entries
    for (const auto* entry : movedEntries) {
        out << entry->ToCommentLine() << "\n";
        out << entry->ToIniLine() << "\n";
        out << "\n";
    }

    // Write deleted entries
    if (!deletedEntries.empty()) {
        out << "; --- Deleted References (" << deletedEntries.size() << ") ---\n";
        for (const auto* entry : deletedEntries) {
            out << entry->ToCommentLine() << "\n";
            out << entry->ToIniLine() << "\n";
            out << "\n";
        }
    }
}

bool BaseObjectSwapperParser::WriteConsolidatedIniFile(
    const std::filesystem::path& filePath,
    const std::vector<CellSectionData>& cellSections) const
{
    if (cellSections.empty()) {
        return true;  // Nothing to write
    }

    auto latestFilePath = GetLatestFilePath(filePath);

    // Read existing entries from latest or swap file and organize by cell
    std::unordered_map<std::string, std::unordered_map<std::string, BOSTransformEntry>> existingEntriesByCell;

    if (std::filesystem::exists(latestFilePath)) {
        auto existingEntries = ParseIniFile(latestFilePath);
        // Note: In consolidated mode, we need to parse cell info from comments
        // For now, existing entries go to a "Unknown" cell if we can't determine
        for (auto& entry : existingEntries) {
            existingEntriesByCell[""][""][entry.formKeyString] = std::move(entry);
        }
    } else if (std::filesystem::exists(filePath)) {
        auto existingEntries = ParseIniFile(filePath);
        for (auto& entry : existingEntries) {
            existingEntriesByCell[""][entry.formKeyString] = std::move(entry);
        }
    }

    // Prepare final cell data
    std::vector<CellSectionData> finalSections;

    for (const auto& section : cellSections) {
        CellSectionData finalSection;
        finalSection.cellFormKey = section.cellFormKey;
        finalSection.cellEditorId = section.cellEditorId;

        // Merge with existing entries for this cell
        std::unordered_map<std::string, BOSTransformEntry> mergedEntries;

        // Start with existing entries
        auto existingIt = existingEntriesByCell.find(section.cellFormKey);
        if (existingIt != existingEntriesByCell.end()) {
            for (auto& [key, entry] : existingIt->second) {
                mergedEntries[key] = std::move(entry);
            }
        }

        // Merge in new entries
        for (const auto& entry : section.entries) {
            auto it = mergedEntries.find(entry.formKeyString);
            if (it != mergedEntries.end()) {
                BOSTransformEntry merged = entry;
                EntryMetadata existingMeta = it->second.GetMetadata();
                EntryMetadata newMeta = merged.GetMetadata();
                newMeta.MergeFrom(existingMeta);
                merged.SetMetadata(newMeta);
                mergedEntries[entry.formKeyString] = std::move(merged);
            } else {
                mergedEntries[entry.formKeyString] = entry;
            }
        }

        // Convert map back to vector
        for (auto& [key, entry] : mergedEntries) {
            finalSection.entries.push_back(std::move(entry));
        }

        if (!finalSection.entries.empty()) {
            finalSections.push_back(std::move(finalSection));
        }
    }

    // Write to the latest file
    std::ofstream file(latestFilePath, std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("BaseObjectSwapperParser: Failed to open {} for writing",
            latestFilePath.string());
        return false;
    }

    // Write file header
    WriteFileHeader(file);

    // Write [Transforms] section header
    file << "[Transforms]\n";
    file << "\n";

    size_t totalEntries = 0;

    // Write each cell section
    for (const auto& section : finalSections) {
        // Collect plugins and separate moved/deleted for this cell
        std::set<std::string> plugins;
        std::vector<const BOSTransformEntry*> movedEntries;
        std::vector<const BOSTransformEntry*> deletedEntries;

        for (const auto& entry : section.entries) {
            std::string plugin = entry.GetPluginName();
            if (!plugin.empty()) {
                plugins.insert(plugin);
            }

            if (entry.isDeleted) {
                deletedEntries.push_back(&entry);
            } else {
                movedEntries.push_back(&entry);
            }
        }

        // Determine cell name (prefer editor ID)
        std::string cellName = section.cellEditorId.empty() ? section.cellFormKey : section.cellEditorId;

        WriteCellSection(file, cellName, section.cellFormKey, movedEntries, deletedEntries, plugins);

        totalEntries += section.entries.size();
    }

    file.flush();
    if (file.fail()) {
        spdlog::error("BaseObjectSwapperParser: Failed to write data to {}", latestFilePath.string());
        return false;
    }

    spdlog::info("BaseObjectSwapperParser: Wrote {} entries across {} cells to consolidated file {}",
        totalEntries, finalSections.size(), latestFilePath.filename().string());

    return true;
}

std::filesystem::path BaseObjectSwapperParser::GetDataFolderPath() const
{
    // Get path relative to Skyrim's Data folder
    // SKSE provides this through various means, but we can construct it
    // from the executable path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::filesystem::path skyrimPath(exePath);
    skyrimPath = skyrimPath.parent_path();  // Remove executable name

    return skyrimPath / "Data";
}

std::filesystem::path BaseObjectSwapperParser::GetVREditorFolderPath() const
{
    auto vrEditorPath = GetDataFolderPath() / "SKSE" / "Plugins" / "VREditor";

    // Create the folder if it doesn't exist
    if (!std::filesystem::exists(vrEditorPath)) {
        std::error_code ec;
        std::filesystem::create_directories(vrEditorPath, ec);
        if (ec) {
            spdlog::error("BaseObjectSwapperParser: Failed to create VREditor folder: {}", ec.message());
        } else {
            spdlog::info("BaseObjectSwapperParser: Created VREditor folder at {}", vrEditorPath.string());
        }
    }

    return vrEditorPath;
}

std::string BaseObjectSwapperParser::BuildIniFileName(std::string_view cellEditorId, std::string_view cellFormKey)
{
    // Prefer editor ID, fall back to FormKey
    std::string identifier;
    if (!cellEditorId.empty()) {
        identifier = SanitizeForFilename(cellEditorId);
    } else {
        identifier = SanitizeForFilename(cellFormKey);
    }

    if (identifier.empty()) {
        identifier = "Unknown";
    }
    return "VREditor_" + identifier + "_SWAP.ini";
}

std::string BaseObjectSwapperParser::BuildLatestIniFileName(std::string_view cellEditorId, std::string_view cellFormKey)
{
    // Prefer editor ID, fall back to FormKey
    std::string identifier;
    if (!cellEditorId.empty()) {
        identifier = SanitizeForFilename(cellEditorId);
    } else {
        identifier = SanitizeForFilename(cellFormKey);
    }

    if (identifier.empty()) {
        identifier = "Unknown";
    }
    return "VREditor_" + identifier + "_SWAP_latest.ini";
}

std::filesystem::path BaseObjectSwapperParser::GetLatestFilePath(const std::filesystem::path& swapFilePath)
{
    // Latest files stay in the same Data/ folder as swap files
    // Convert "VREditor_Location_SWAP.ini" to "VREditor_Location_SWAP_latest.ini"
    std::string filename = swapFilePath.filename().string();

    // Find "_SWAP.ini" and insert "_latest" before ".ini"
    const std::string swapSuffix = "_SWAP.ini";
    if (filename.size() >= swapSuffix.size() &&
        filename.substr(filename.size() - swapSuffix.size()) == swapSuffix) {
        // Replace "_SWAP.ini" with "_SWAP_latest.ini"
        filename = filename.substr(0, filename.size() - 4) + "_latest.ini";
    }

    // Stay in the same folder as swap file (Data/)
    return swapFilePath.parent_path() / filename;
}

std::filesystem::path BaseObjectSwapperParser::GetSwapFilePath(const std::filesystem::path& latestFilePath)
{
    // Both swap and latest files are in Data/ folder
    // Convert "VREditor_Location_SWAP_latest.ini" to "VREditor_Location_SWAP.ini"
    std::string filename = latestFilePath.filename().string();

    // Find "_SWAP_latest.ini" and replace with "_SWAP.ini"
    const std::string latestSuffix = "_SWAP_latest.ini";
    if (filename.size() >= latestSuffix.size() &&
        filename.substr(filename.size() - latestSuffix.size()) == latestSuffix) {
        // Replace "_SWAP_latest.ini" with "_SWAP.ini"
        filename = filename.substr(0, filename.size() - latestSuffix.size()) + "_SWAP.ini";
    }

    // Both files are in the same folder (Data/)
    return latestFilePath.parent_path() / filename;
}

void BaseObjectSwapperParser::ApplyPendingLatestFiles() const
{
    // Latest files are stored in Data/ folder alongside swap files
    auto dataPath = GetDataFolderPath();

    if (!std::filesystem::exists(dataPath)) {
        spdlog::trace("BaseObjectSwapperParser: Data folder doesn't exist, no latest files to apply");
        return;
    }

    spdlog::info("BaseObjectSwapperParser: Checking for pending latest files in Data folder...");

    int appliedCount = 0;

    for (const auto& entry : std::filesystem::directory_iterator(dataPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        auto filename = entry.path().filename().string();

        // Look for VREditor_*_SWAP_latest.ini files
        if (filename.starts_with("VREditor_") && filename.ends_with("_SWAP_latest.ini")) {
            auto latestPath = entry.path();
            auto swapPath = GetSwapFilePath(latestPath);

            spdlog::info("BaseObjectSwapperParser: Found latest file {}, applying to {}",
                latestPath.filename().string(), swapPath.filename().string());

            // Copy latest file contents to swap file
            // We do a full copy (not rename) so we keep the latest file as a backup
            // until the next successful write
            std::error_code ec;
            std::filesystem::copy_file(latestPath, swapPath,
                std::filesystem::copy_options::overwrite_existing, ec);

            if (ec) {
                spdlog::error("BaseObjectSwapperParser: Failed to copy {} to {}: {}",
                    latestPath.filename().string(), swapPath.filename().string(), ec.message());
            } else {
                appliedCount++;
                spdlog::info("BaseObjectSwapperParser: Successfully applied {} to {}",
                    latestPath.filename().string(), swapPath.filename().string());

                // Delete the latest file after successful copy
                std::filesystem::remove(latestPath, ec);
                if (ec) {
                    spdlog::warn("BaseObjectSwapperParser: Failed to delete latest file {}: {}",
                        latestPath.filename().string(), ec.message());
                }
            }
        }
    }

    if (appliedCount > 0) {
        spdlog::info("BaseObjectSwapperParser: Applied {} latest file(s) to swap files", appliedCount);
    } else {
        spdlog::trace("BaseObjectSwapperParser: No pending latest files found");
    }
}

std::string BaseObjectSwapperParser::SanitizeForFilename(std::string_view input)
{
    std::string result;
    result.reserve(input.size());

    for (char c : input) {
        // Replace invalid filename characters with underscore
        if (c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            result += '_';
        } else if (c == ' ') {
            result += '_';  // Replace spaces with underscores
        } else {
            result += c;
        }
    }

    // Remove leading/trailing underscores
    while (!result.empty() && result.front() == '_') {
        result.erase(0, 1);
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    return result;
}

bool BaseObjectSwapperParser::ParsePropertyString(std::string_view props, BOSTransformEntry& entry)
{
    // Initialize defaults
    entry.position = RE::NiPoint3(0, 0, 0);
    entry.rotation = RE::NiPoint3(0, 0, 0);
    entry.scale = 1.0f;
    entry.isDeleted = false;

    bool hasPosition = false;

    try {
        // Regular expressions for matching property patterns
        // posA(x,y,z)
        std::regex posPattern(R"(posA\s*\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*\))");
        // rotA(x,y,z)
        std::regex rotPattern(R"(rotA\s*\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*\))");
        // scaleA(s)
        std::regex scalePattern(R"(scaleA\s*\(\s*([+-]?\d*\.?\d+)\s*\))");
        // flags(0x...) - detect Initially Disabled flag
        std::regex flagsPattern(R"(flags\s*\(\s*0x([0-9A-Fa-f]+)\s*\))");

        std::string propsStr(props);
        std::smatch match;

        // Parse position
        if (std::regex_search(propsStr, match, posPattern)) {
            entry.position.x = std::stof(match[1].str());
            entry.position.y = std::stof(match[2].str());
            entry.position.z = std::stof(match[3].str());
            hasPosition = true;
        }

        // Parse rotation
        if (std::regex_search(propsStr, match, rotPattern)) {
            entry.rotation.x = std::stof(match[1].str());
            entry.rotation.y = std::stof(match[2].str());
            entry.rotation.z = std::stof(match[3].str());
        }

        // Parse scale
        if (std::regex_search(propsStr, match, scalePattern)) {
            entry.scale = std::stof(match[1].str());
        }

        // Parse flags - check for Initially Disabled flag
        if (std::regex_search(propsStr, match, flagsPattern)) {
            uint32_t flags = std::stoul(match[1].str(), nullptr, 16);
            if (flags & INITIALLY_DISABLED_FLAG) {
                entry.isDeleted = true;
            }
        }
    } catch (const std::invalid_argument& e) {
        spdlog::error("BaseObjectSwapperParser: Invalid number format in properties '{}': {}",
            props, e.what());
        return false;
    } catch (const std::out_of_range& e) {
        spdlog::error("BaseObjectSwapperParser: Number out of range in properties '{}': {}",
            props, e.what());
        return false;
    } catch (const std::regex_error& e) {
        spdlog::error("BaseObjectSwapperParser: Regex error parsing properties '{}': {}",
            props, e.what());
        return false;
    }

    // We need at least position to be valid
    return hasPosition;
}

std::string BaseObjectSwapperParser::FormatFloat(float value)
{
    // Format with up to 4 decimal places, removing trailing zeros
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4) << value;
    std::string result = ss.str();

    // Remove trailing zeros after decimal point
    size_t dotPos = result.find('.');
    if (dotPos != std::string::npos) {
        size_t lastNonZero = result.find_last_not_of('0');
        if (lastNonZero > dotPos) {
            result.erase(lastNonZero + 1);
        } else {
            result.erase(dotPos);  // Remove decimal point too if no decimals
        }
    }

    return result;
}

} // namespace Persistence
