#include "AddedObjectsParser.h"
#include "FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESModel.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <iomanip>
#include <cmath>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Persistence {

// ============================================================================
// AddedObjectEntry
// ============================================================================

std::string AddedObjectEntry::ToIniLine() const
{
    std::ostringstream ss;

    // Format: baseForm|posA(x,y,z),rotA(rx,ry,rz),scaleA(s)
    ss << baseFormString << "|";

    // Position
    ss << "posA(" << AddedObjectsParser::FormatFloat(position.x) << ","
       << AddedObjectsParser::FormatFloat(position.y) << ","
       << AddedObjectsParser::FormatFloat(position.z) << ")";

    // Rotation
    ss << ",rotA(" << AddedObjectsParser::FormatFloat(rotation.x) << ","
       << AddedObjectsParser::FormatFloat(rotation.y) << ","
       << AddedObjectsParser::FormatFloat(rotation.z) << ")";

    // Scale (only if not 1.0)
    if (std::abs(scale - 1.0f) > 0.0001f) {
        ss << ",scaleA(" << AddedObjectsParser::FormatFloat(scale) << ")";
    }

    return ss.str();
}

std::string AddedObjectEntry::ToCommentLine() const
{
    // Use unified pipe-separated format: ; EditorId|DisplayName|MeshPath
    EntryMetadata metadata;
    metadata.editorId = editorId;
    metadata.displayName = displayName;
    metadata.meshName = meshName;
    return metadata.ToCommentLine();
}

void AddedObjectEntry::ApplyMetadataFromComment(std::string_view commentLine)
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

EntryMetadata AddedObjectEntry::GetMetadata() const
{
    EntryMetadata metadata;
    metadata.editorId = editorId;
    metadata.displayName = displayName;
    metadata.meshName = meshName;
    metadata.formTypeName = formTypeName;
    return metadata;
}

void AddedObjectEntry::SetMetadata(const EntryMetadata& metadata)
{
    editorId = metadata.editorId;
    displayName = metadata.displayName;
    meshName = metadata.meshName;
    formTypeName = metadata.formTypeName;
}

std::string AddedObjectEntry::GetPluginName() const
{
    return ExtractPluginFromFormKey(baseFormString);
}

std::optional<AddedObjectEntry> AddedObjectEntry::FromIniLine(std::string_view line)
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

    // Need at least baseForm|properties
    if (parts.size() < 2) {
        return std::nullopt;
    }

    AddedObjectEntry entry;
    entry.baseFormString = parts[0];

    // Trim whitespace from baseFormString
    entry.baseFormString.erase(0, entry.baseFormString.find_first_not_of(" \t"));
    entry.baseFormString.erase(entry.baseFormString.find_last_not_of(" \t") + 1);

    // Parse properties
    if (!AddedObjectsParser::ParsePropertyString(parts[1], entry)) {
        return std::nullopt;
    }

    return entry;
}

// ============================================================================
// AddedObjectsParser
// ============================================================================

AddedObjectsParser* AddedObjectsParser::GetSingleton()
{
    static AddedObjectsParser instance;
    return &instance;
}

AddedObjectsFileData AddedObjectsParser::ParseIniFile(const std::filesystem::path& filePath) const
{
    AddedObjectsFileData data;
    data.iniFileName = filePath.filename().string();

    if (!std::filesystem::exists(filePath)) {
        return data;
    }

    // First, extract cell FormKey from header
    data.cellFormKey = ExtractCellFormKeyFromHeader(filePath);

    std::ifstream file(filePath);
    if (!file.is_open()) {
        spdlog::warn("AddedObjectsParser: Failed to open {}", filePath.string());
        return data;
    }

    std::string line;
    std::string lastComment;  // Track the comment line preceding each entry
    bool inAddedObjectsSection = false;

    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) {
            // Empty line resets comment tracking
            lastComment.clear();
            continue;
        }

        // Check for Cell EditorID in comments
        if (line.starts_with("; VR Editor Added Objects - Cell:")) {
            // Extract cell editor ID from header
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                data.cellEditorId = line.substr(colonPos + 1);
                data.cellEditorId.erase(0, data.cellEditorId.find_first_not_of(" \t"));
                data.cellEditorId.erase(data.cellEditorId.find_last_not_of(" \t") + 1);
            }
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
            // Check if this is [AddedObjects] section
            inAddedObjectsSection = (line.find("[AddedObjects]") == 0);
            lastComment.clear();
            continue;
        }

        // Only parse lines in [AddedObjects] section
        if (inAddedObjectsSection) {
            auto entry = AddedObjectEntry::FromIniLine(line);
            if (entry) {
                // Apply metadata from the preceding comment line if present
                if (!lastComment.empty()) {
                    entry->ApplyMetadataFromComment(lastComment);
                }
                data.entries.push_back(std::move(*entry));
            }
            lastComment.clear();
        }
    }

    spdlog::trace("AddedObjectsParser: Parsed {} entries from {} (cell: {})",
        data.entries.size(), filePath.string(), data.cellFormKey);

    return data;
}

std::string AddedObjectsParser::ExtractCellFormKeyFromHeader(const std::filesystem::path& filePath) const
{
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return "";
    }

    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));

        // Look for "; Cell FormKey: " line
        if (line.starts_with("; Cell FormKey:")) {
            size_t colonPos = line.find(':', 14);  // Start after "; Cell FormKey"
            if (colonPos != std::string::npos) {
                std::string formKey = line.substr(colonPos + 1);
                formKey.erase(0, formKey.find_first_not_of(" \t"));
                formKey.erase(formKey.find_last_not_of(" \t") + 1);
                return formKey;
            }
        }

        // Stop after header section (first blank line or section header)
        if (line.empty() || line[0] == '[') {
            break;
        }
    }

    return "";
}

std::vector<std::filesystem::path> AddedObjectsParser::FindAllAddedObjectsIniFiles() const
{
    std::vector<std::filesystem::path> files;
    auto vrEditorPath = GetVREditorFolderPath();

    if (!std::filesystem::exists(vrEditorPath)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(vrEditorPath)) {
        if (entry.is_regular_file()) {
            auto filename = entry.path().filename().string();
            // Check for VREditor_*_AddedObjects.ini pattern
            if (filename.starts_with("VREditor_") && filename.ends_with("_AddedObjects.ini")) {
                files.push_back(entry.path());
            }
        }
    }

    return files;
}

bool AddedObjectsParser::WriteIniFile(const std::filesystem::path& filePath,
                                       const std::string& cellFormKey,
                                       const std::string& cellEditorId,
                                       const std::vector<AddedObjectEntry>& entries) const
{
    if (entries.empty()) {
        return true;  // Nothing to write
    }

    // Read existing entries and merge
    auto existingData = ParseIniFile(filePath);
    std::vector<AddedObjectEntry> mergedEntries;

    // Use position as unique key for deduplication
    auto positionKey = [](const AddedObjectEntry& e) {
        return fmt::format("{:.2f},{:.2f},{:.2f}", e.position.x, e.position.y, e.position.z);
    };

    std::unordered_map<std::string, size_t> positionToIndex;

    // Add existing entries first
    for (auto& entry : existingData.entries) {
        std::string key = positionKey(entry);
        positionToIndex[key] = mergedEntries.size();
        mergedEntries.push_back(std::move(entry));
    }

    // Smart merge: add/update with new entries, preserving existing metadata when new is empty
    for (const auto& entry : entries) {
        std::string key = positionKey(entry);
        auto it = positionToIndex.find(key);
        if (it != positionToIndex.end()) {
            // Entry exists - merge metadata (preserve existing if new is empty)
            AddedObjectEntry merged = entry;
            EntryMetadata existingMeta = mergedEntries[it->second].GetMetadata();
            EntryMetadata newMeta = merged.GetMetadata();
            newMeta.MergeFrom(existingMeta);  // Fill empty fields from existing
            merged.SetMetadata(newMeta);
            mergedEntries[it->second] = std::move(merged);
        } else {
            // Add new
            positionToIndex[key] = mergedEntries.size();
            mergedEntries.push_back(entry);
        }
    }

    // Write to a temporary file first, then rename for atomic operation
    std::filesystem::path tempPath = filePath;
    tempPath += ".tmp";

    try {
        std::ofstream file(tempPath, std::ios::trunc);
        if (!file.is_open()) {
            spdlog::error("AddedObjectsParser: Failed to create temp file {} for writing",
                tempPath.string());
            return false;
        }

        // Write header comment
        file << "; ============================================================\n";
        file << "; VR Editor Added Objects - Cell: " << (cellEditorId.empty() ? cellFormKey : cellEditorId) << "\n";
        file << "; Auto-generated by In-Game Patcher VR\n";
        file << "; ============================================================\n";
        file << ";\n";
        file << "; IMPORTANT: This file differs from _SWAP.ini files!\n";
        file << "; - _SWAP.ini: Repositions EXISTING world references (uses Base Object Swapper)\n";
        file << "; - _AddedObjects.ini: tracks NEWLY SPAWNED objects from base forms (Either from duplicate button or gallery)\n";
        file << ";\n";
        file << "; This file currently only serves as a log for your added objects\n";
        file << "; the actual added objects are stored in the game save file. \n";
        file << ";\n";
        file << "; Cell FormKey: " << cellFormKey << "\n";
        file << ";\n";
        file << "; Format: baseForm|posA(x,y,z),rotA(rx,ry,rz),scaleA(s)\n";
        file << "; - baseForm: EditorID or FormKey (0xID~Plugin) of the base object to spawn\n";
        file << "; ============================================================\n";
        file << "\n";

        // Write [AddedObjects] section
        file << "[AddedObjects]\n";
        file << "; Added objects (" << mergedEntries.size() << " entries)\n";
        file << "\n";

        for (const auto& entry : mergedEntries) {
            // Always write comment line for consistency (enables metadata preservation on merge)
            file << entry.ToCommentLine() << "\n";
            file << entry.ToIniLine() << "\n";
            file << "\n";
        }

        // Ensure all data is flushed to disk
        file.flush();
        if (file.fail()) {
            spdlog::error("AddedObjectsParser: Failed to flush data to {}", tempPath.string());
            file.close();
            std::filesystem::remove(tempPath);
            return false;
        }
        file.close();

        // Atomic overwrite
#ifdef _WIN32
        if (!MoveFileExW(tempPath.wstring().c_str(),
                         filePath.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DWORD error = GetLastError();
            spdlog::error("AddedObjectsParser: Failed to move temp file to {}: Windows error {}",
                filePath.string(), error);
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return false;
        }
#else
        std::error_code ec;
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath, ec);
            if (ec) {
                spdlog::error("AddedObjectsParser: Failed to remove old file {}: {}",
                    filePath.string(), ec.message());
                std::filesystem::remove(tempPath);
                return false;
            }
        }

        std::filesystem::rename(tempPath, filePath, ec);
        if (ec) {
            spdlog::error("AddedObjectsParser: Failed to rename temp file to {}: {}",
                filePath.string(), ec.message());
            std::filesystem::remove(tempPath);
            return false;
        }
#endif

        spdlog::info("AddedObjectsParser: Wrote {} entries to {}",
            mergedEntries.size(), filePath.string());

        return true;

    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("AddedObjectsParser: Filesystem error writing {}: {}",
            filePath.string(), e.what());
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return false;
    }
}

bool AddedObjectsParser::WriteFileData(const AddedObjectsFileData& data) const
{
    auto vrEditorPath = GetVREditorFolderPath();
    auto filePath = vrEditorPath / data.iniFileName;

    return WriteIniFile(filePath, data.cellFormKey, data.cellEditorId, data.entries);
}

std::filesystem::path AddedObjectsParser::GetVREditorFolderPath() const
{
    // Get path relative to Skyrim's Data/VREditor folder
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::filesystem::path skyrimPath(exePath);
    skyrimPath = skyrimPath.parent_path();  // Remove executable name

    auto vrEditorPath = skyrimPath / "Data" / "SKSE" / "Plugins" / "VREditor";

    // Create the folder if it doesn't exist
    if (!std::filesystem::exists(vrEditorPath)) {
        std::error_code ec;
        std::filesystem::create_directories(vrEditorPath, ec);
        if (ec) {
            spdlog::error("AddedObjectsParser: Failed to create VREditor folder: {}", ec.message());
        } else {
            spdlog::info("AddedObjectsParser: Created VREditor folder at {}", vrEditorPath.string());
        }
    }

    return vrEditorPath;
}

std::string AddedObjectsParser::BuildIniFileName(RE::TESObjectCELL* cell)
{
    if (!cell) {
        return "VREditor_Unknown_AddedObjects.ini";
    }

    std::string cellIdentifier;

    // Prefer EditorID
    const char* editorId = cell->GetFormEditorID();
    if (editorId && editorId[0] != '\0') {
        cellIdentifier = editorId;
    } else {
        // Use FormKey (with underscore instead of tilde for filename safety)
        std::string formKey = FormKeyUtil::BuildFormKey(cell);
        // Replace ~ with _ for filename safety
        std::replace(formKey.begin(), formKey.end(), '~', '_');
        cellIdentifier = formKey;
    }

    return BuildIniFileName(cellIdentifier, FormKeyUtil::BuildFormKey(cell));
}

std::string AddedObjectsParser::BuildIniFileName(const std::string& cellEditorId, const std::string& cellFormKey)
{
    std::string cellIdentifier;

    if (!cellEditorId.empty()) {
        cellIdentifier = cellEditorId;
    } else if (!cellFormKey.empty()) {
        // Use FormKey with underscore instead of tilde
        cellIdentifier = cellFormKey;
        std::replace(cellIdentifier.begin(), cellIdentifier.end(), '~', '_');
    } else {
        cellIdentifier = "Unknown";
    }

    std::string sanitized = SanitizeForFilename(cellIdentifier);
    if (sanitized.empty()) {
        sanitized = "Unknown";
    }

    return "VREditor_" + sanitized + "_AddedObjects.ini";
}

std::string AddedObjectsParser::SanitizeForFilename(std::string_view input)
{
    std::string result;
    result.reserve(input.size());

    for (char c : input) {
        // Replace invalid filename characters with underscore
        if (c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || c == '~') {
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

bool AddedObjectsParser::ParsePropertyString(std::string_view props, AddedObjectEntry& entry)
{
    // Initialize defaults
    entry.position = RE::NiPoint3(0, 0, 0);
    entry.rotation = RE::NiPoint3(0, 0, 0);
    entry.scale = 1.0f;

    bool hasPosition = false;

    try {
        // Regular expressions for matching property patterns
        std::regex posPattern(R"(posA\s*\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*\))");
        std::regex rotPattern(R"(rotA\s*\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*\))");
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
        spdlog::error("AddedObjectsParser: Invalid number format in properties '{}': {}",
            props, e.what());
        return false;
    } catch (const std::out_of_range& e) {
        spdlog::error("AddedObjectsParser: Number out of range in properties '{}': {}",
            props, e.what());
        return false;
    } catch (const std::regex_error& e) {
        spdlog::error("AddedObjectsParser: Regex error parsing properties '{}': {}",
            props, e.what());
        return false;
    }

    // We need at least position to be valid
    return hasPosition;
}

std::string AddedObjectsParser::FormatFloat(float value)
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

RE::TESForm* AddedObjectsParser::ResolveBaseForm(const std::string& baseFormString)
{
    if (baseFormString.empty()) {
        return nullptr;
    }

    // First, try as EditorID
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler) {
        auto* form = RE::TESForm::LookupByEditorID(baseFormString);
        if (form) {
            spdlog::trace("AddedObjectsParser: Resolved '{}' via EditorID", baseFormString);
            return form;
        }
    }

    // Second, try as FormKey
    RE::FormID runtimeFormId = FormKeyUtil::ResolveToRuntimeFormID(baseFormString);
    if (runtimeFormId != 0) {
        auto* form = RE::TESForm::LookupByID(runtimeFormId);
        if (form) {
            spdlog::trace("AddedObjectsParser: Resolved '{}' via FormKey", baseFormString);
            return form;
        }
    }

    spdlog::warn("AddedObjectsParser: Could not resolve base form '{}'", baseFormString);
    return nullptr;
}

} // namespace Persistence
