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

    // Format: formKey|posA(x,y,z),rotA(rx,ry,rz),scaleA(s)|100
    ss << formKeyString << "|";

    // Position - for deleted references, use the deletion Z coordinate
    float finalZ = isDeleted ? DELETED_REFERENCE_Z : position.z;

    ss << "posA(" << BaseObjectSwapperParser::FormatFloat(position.x) << ","
       << BaseObjectSwapperParser::FormatFloat(position.y) << ","
       << BaseObjectSwapperParser::FormatFloat(finalZ) << ")";

    // Rotation
    ss << ",rotA(" << BaseObjectSwapperParser::FormatFloat(rotation.x) << ","
       << BaseObjectSwapperParser::FormatFloat(rotation.y) << ","
       << BaseObjectSwapperParser::FormatFloat(rotation.z) << ")";

    // Scale (only if not 1.0)
    if (std::abs(scale - 1.0f) > 0.0001f) {
        ss << ",scaleA(" << BaseObjectSwapperParser::FormatFloat(scale) << ")";
    }

    // Chance is always 100
    ss << "|100";

    return ss.str();
}

std::string BOSTransformEntry::ToCommentLine() const
{
    std::ostringstream ss;
    ss << "; ";

    // Add editor ID if available
    if (!editorId.empty()) {
        ss << editorId;
    }

    // Add display name if available and different from editor ID
    if (!displayName.empty() && displayName != editorId) {
        if (!editorId.empty()) {
            ss << " - ";
        }
        ss << "\"" << displayName << "\"";
    }

    // Add mesh name if available
    if (!meshName.empty()) {
        if (!editorId.empty() || !displayName.empty()) {
            ss << " | ";
        }
        ss << meshName;
    }

    return ss.str();
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
    bool inTransformsSection = false;

    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // Check for section header
        if (line[0] == '[') {
            // Check if this is [Transforms] section (with optional location filter)
            inTransformsSection = (line.find("[Transforms") == 0);
            continue;
        }

        // Only parse lines in [Transforms] section
        if (inTransformsSection) {
            auto entry = BOSTransformEntry::FromIniLine(line);
            if (entry) {
                entries.push_back(std::move(*entry));
            }
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

    // Determine the session file path - we write to _session.ini to avoid BOS file lock
    // filePath is the "canonical" swap file path (e.g., VREditor_Location_SWAP.ini)
    // We write to Data/VREditor/VREditor_Location_SWAP_session.ini instead
    auto sessionFilePath = GetSessionFilePath(filePath);

    // Ensure the VREditor folder exists
    auto sessionDir = sessionFilePath.parent_path();
    if (!std::filesystem::exists(sessionDir)) {
        std::error_code ec;
        std::filesystem::create_directories(sessionDir, ec);
        if (ec) {
            spdlog::error("BaseObjectSwapperParser: Failed to create directory {}: {}",
                sessionDir.string(), ec.message());
            return false;
        }
        spdlog::info("BaseObjectSwapperParser: Created directory {}", sessionDir.string());
    }

    // Read existing entries and merge
    // Priority: session file first (our pending changes), then swap file (BOS baseline)
    std::unordered_map<std::string, BOSTransformEntry> mergedEntries;

    // Load existing entries - prefer session file if it exists, otherwise use swap file
    {
        std::vector<BOSTransformEntry> existingEntries;
        if (std::filesystem::exists(sessionFilePath)) {
            // Session file exists - use it (contains our accumulated changes)
            existingEntries = ParseIniFile(sessionFilePath);
            spdlog::trace("BaseObjectSwapperParser: Merging with existing session file {}",
                sessionFilePath.filename().string());
        } else if (std::filesystem::exists(filePath)) {
            // No session file yet - start from the swap file (BOS baseline)
            existingEntries = ParseIniFile(filePath);
            spdlog::trace("BaseObjectSwapperParser: Starting from swap file {}",
                filePath.filename().string());
        }

        for (const auto& entry : existingEntries) {
            mergedEntries[entry.formKeyString] = entry;
        }
    }

    // Overwrite/add new entries
    for (const auto& entry : entries) {
        mergedEntries[entry.formKeyString] = entry;
    }

    // Collect unique plugin names and separate moved vs deleted entries
    std::set<std::string> pluginNames;
    std::vector<const BOSTransformEntry*> movedEntries;
    std::vector<const BOSTransformEntry*> deletedEntries;

    for (const auto& [key, entry] : mergedEntries) {
        if (!entry.pluginName.empty()) {
            pluginNames.insert(entry.pluginName);
        }

        if (entry.isDeleted) {
            deletedEntries.push_back(&entry);
        } else {
            movedEntries.push_back(&entry);
        }
    }

    // Write to the SESSION file (not the swap file) to avoid BOS file lock
    // BOS locks _SWAP.ini files when loading, but doesn't know about _session.ini
    // On next game start, ApplyPendingSessionFiles() will copy this to the swap file
    std::ofstream file(sessionFilePath, std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("BaseObjectSwapperParser: Failed to open {} for writing",
            sessionFilePath.string());
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
        // Write comment with editor ID and name
        file << entry->ToCommentLine() << "\n";
        file << entry->ToIniLine() << "\n";
        file << "\n";
    }

    // Write deleted entries section if any
    if (!deletedEntries.empty()) {
        file << "; ============================================================\n";
        file << "; DELETED REFERENCES (" << deletedEntries.size() << " entries)\n";
        file << "; These objects are moved far below the world (Z = "
             << FormatFloat(DELETED_REFERENCE_Z) << ")\n";
        file << "; ============================================================\n";
        file << "\n";

        for (const auto* entry : deletedEntries) {
            // Write comment with editor ID and name
            file << entry->ToCommentLine() << "\n";
            file << entry->ToIniLine() << "\n";
            file << "\n";
        }
    }

    // Ensure all data is flushed to disk
    file.flush();
    if (file.fail()) {
        spdlog::error("BaseObjectSwapperParser: Failed to write data to {}", sessionFilePath.string());
        return false;
    }

    spdlog::info("BaseObjectSwapperParser: Wrote {} entries ({} moved, {} deleted) to session file {}",
        mergedEntries.size(), movedEntries.size(), deletedEntries.size(), sessionFilePath.filename().string());

    return true;
}

bool BaseObjectSwapperParser::WriteCellData(const BOSCellData& data) const
{
    auto dataPath = GetDataFolderPath();
    auto filePath = dataPath / data.iniFileName;

    return WriteIniFile(filePath, data.entries);
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
    auto vrEditorPath = GetDataFolderPath() / "VREditor";

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

std::string BaseObjectSwapperParser::BuildSessionIniFileName(std::string_view cellEditorId, std::string_view cellFormKey)
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
    return "VREditor_" + identifier + "_SWAP_session.ini";
}

std::filesystem::path BaseObjectSwapperParser::GetSessionFilePath(const std::filesystem::path& swapFilePath)
{
    // Session files go in Data/VREditor/ subfolder, not alongside swap files
    // Convert "VREditor_Location_SWAP.ini" to "VREditor_Location_SWAP_session.ini"
    std::string filename = swapFilePath.filename().string();

    // Find "_SWAP.ini" and insert "_session" before ".ini"
    const std::string swapSuffix = "_SWAP.ini";
    if (filename.size() >= swapSuffix.size() &&
        filename.substr(filename.size() - swapSuffix.size()) == swapSuffix) {
        // Replace "_SWAP.ini" with "_SWAP_session.ini"
        filename = filename.substr(0, filename.size() - 4) + "_session.ini";
    }

    // Put in VREditor subfolder (swapFilePath is in Data/, session goes to Data/VREditor/)
    return swapFilePath.parent_path() / "VREditor" / filename;
}

std::filesystem::path BaseObjectSwapperParser::GetSwapFilePath(const std::filesystem::path& sessionFilePath)
{
    // Swap files go in Data/ (parent of VREditor/), session files are in Data/VREditor/
    // Convert "VREditor_Location_SWAP_session.ini" to "VREditor_Location_SWAP.ini"
    std::string filename = sessionFilePath.filename().string();

    // Find "_SWAP_session.ini" and replace with "_SWAP.ini"
    const std::string sessionSuffix = "_SWAP_session.ini";
    if (filename.size() >= sessionSuffix.size() &&
        filename.substr(filename.size() - sessionSuffix.size()) == sessionSuffix) {
        // Replace "_SWAP_session.ini" with "_SWAP.ini"
        filename = filename.substr(0, filename.size() - sessionSuffix.size()) + "_SWAP.ini";
    }

    // Session file is in Data/VREditor/, swap file goes to Data/ (parent directory)
    return sessionFilePath.parent_path().parent_path() / filename;
}

void BaseObjectSwapperParser::ApplyPendingSessionFiles() const
{
    // Session files are stored in Data/VREditor/
    auto vrEditorPath = GetDataFolderPath() / "VREditor";

    if (!std::filesystem::exists(vrEditorPath)) {
        spdlog::trace("BaseObjectSwapperParser: VREditor folder doesn't exist, no session files to apply");
        return;
    }

    spdlog::info("BaseObjectSwapperParser: Checking for pending session files in VREditor folder...");

    int appliedCount = 0;

    for (const auto& entry : std::filesystem::directory_iterator(vrEditorPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        auto filename = entry.path().filename().string();

        // Look for VREditor_*_SWAP_session.ini files
        if (filename.starts_with("VREditor_") && filename.ends_with("_SWAP_session.ini")) {
            auto sessionPath = entry.path();
            auto swapPath = GetSwapFilePath(sessionPath);

            spdlog::info("BaseObjectSwapperParser: Found session file {}, applying to {}",
                sessionPath.filename().string(), swapPath.filename().string());

            // Copy session file contents to swap file (in Data/ folder)
            // We do a full copy (not rename) so we keep the session file as a backup
            // until the next successful write
            std::error_code ec;
            std::filesystem::copy_file(sessionPath, swapPath,
                std::filesystem::copy_options::overwrite_existing, ec);

            if (ec) {
                spdlog::error("BaseObjectSwapperParser: Failed to copy {} to {}: {}",
                    sessionPath.filename().string(), swapPath.filename().string(), ec.message());
            } else {
                appliedCount++;
                spdlog::info("BaseObjectSwapperParser: Successfully applied {} to {}",
                    sessionPath.filename().string(), swapPath.filename().string());

                // Delete the session file after successful copy
                std::filesystem::remove(sessionPath, ec);
                if (ec) {
                    spdlog::warn("BaseObjectSwapperParser: Failed to delete session file {}: {}",
                        sessionPath.filename().string(), ec.message());
                }
            }
        }
    }

    if (appliedCount > 0) {
        spdlog::info("BaseObjectSwapperParser: Applied {} session file(s) to swap files", appliedCount);
    } else {
        spdlog::trace("BaseObjectSwapperParser: No pending session files found");
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

    bool hasPosition = false;

    try {
        // Regular expressions for matching property patterns
        // posA(x,y,z)
        std::regex posPattern(R"(posA\s*\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*\))");
        // rotA(x,y,z)
        std::regex rotPattern(R"(rotA\s*\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*\))");
        // scaleA(s)
        std::regex scalePattern(R"(scaleA\s*\(\s*([+-]?\d*\.?\d+)\s*\))");

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
