#pragma once

#include "../TestStubs.h"
#include "../../src/persistence/ChangedObjectRegistry.h"
#include "../../src/persistence/BaseObjectSwapperExporter.h"
#include "../../src/persistence/AddedObjectsExporter.h"
#include "../../src/util/UUID.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace TestHelpers {

// =============================================================================
// Test Data Factories
// =============================================================================

// Create a transform with specific position
inline RE::NiTransform MakeTransform(float x, float y, float z, float scale = 1.0f) {
    RE::NiTransform t;
    t.translate = RE::NiPoint3(x, y, z);
    t.scale = scale;
    return t;
}

// Create SaveGameData for an existing object (moved/modified)
inline Persistence::ChangedObjectSaveGameData MakeExistingSaveData(
    const std::string& formKey,
    const std::string& cellFormKey,
    const std::string& cellEditorId = "",
    bool wasDeleted = false)
{
    Persistence::ChangedObjectSaveGameData data;
    data.formKeyString = formKey;
    data.cellFormKey = cellFormKey;
    data.cellEditorId = cellEditorId;
    data.wasDeleted = wasDeleted;
    data.wasCreated = false;
    data.originalTransform = MakeTransform(0, 0, 0);
    data.timestamp = 1000;
    return data;
}

// Create SaveGameData for a created object
inline Persistence::ChangedObjectSaveGameData MakeCreatedSaveData(
    const std::string& formKey,
    const std::string& baseFormKey,
    const std::string& cellFormKey,
    const std::string& cellEditorId = "")
{
    Persistence::ChangedObjectSaveGameData data;
    data.formKeyString = formKey;
    data.baseFormKey = baseFormKey;
    data.cellFormKey = cellFormKey;
    data.cellEditorId = cellEditorId;
    data.wasDeleted = false;
    data.wasCreated = true;
    data.originalTransform = MakeTransform(100, 200, 300);
    data.timestamp = 1000;
    return data;
}

// Create RuntimeData from SaveGameData with pending export flag
inline Persistence::ChangedObjectRuntimeData MakeRuntimeData(
    Persistence::ChangedObjectSaveGameData saveData,
    bool hasPendingExport,
    bool createdThisSession = false)
{
    Persistence::ChangedObjectRuntimeData data;
    data.saveData = std::move(saveData);
    data.hasPendingExportChanges = hasPendingExport;
    data.createdThisSession = createdThisSession;
    data.currentTransform = data.saveData.originalTransform;
    if (createdThisSession) {
        data.firstChangeActionId = Util::UUID::Generate();
    }
    return data;
}

// =============================================================================
// Mock Registry
// =============================================================================

// A testable registry that allows direct manipulation for testing
class MockChangedObjectRegistry : public Persistence::ChangedObjectRegistry {
public:
    // Directly add entries without going through the normal registration flow
    void AddEntryDirect(const std::string& formKey,
                        Persistence::ChangedObjectRuntimeData&& data) {
        // Access private member through LoadEntries workaround
        std::vector<Persistence::ChangedObjectSaveGameData> entries;
        entries.push_back(data.saveData);
        LoadEntries(std::move(entries));

        // Now update the runtime data fields
        // This is a test-only workaround
    }

    // Set pending export flag on an entry
    void SetPendingExport(const std::string& formKey, bool pending) {
        auto entries = GetPendingExportEntries();
        // This is read-only, so we need a different approach
    }
};

// =============================================================================
// Temporary File Manager
// =============================================================================

class TempFileManager {
public:
    TempFileManager() {
        m_tempDir = std::filesystem::temp_directory_path() / "vr_editor_tests";
        std::filesystem::create_directories(m_tempDir);
        std::filesystem::create_directories(m_tempDir / "Data");
        std::filesystem::create_directories(m_tempDir / "Data" / "SKSE" / "Plugins" / "VREditor");
    }

    ~TempFileManager() {
        Cleanup();
    }

    std::filesystem::path GetDataPath() const {
        return m_tempDir / "Data";
    }

    std::filesystem::path GetVREditorPath() const {
        return m_tempDir / "Data" / "SKSE" / "Plugins" / "VREditor";
    }

    std::filesystem::path GetTempPath() const {
        return m_tempDir;
    }

    // Create a file with content
    void CreateFile(const std::string& relativePath, const std::string& content) {
        auto fullPath = m_tempDir / relativePath;
        std::filesystem::create_directories(fullPath.parent_path());
        std::ofstream file(fullPath);
        file << content;
    }

    // Read a file's content
    std::string ReadFile(const std::string& relativePath) {
        auto fullPath = m_tempDir / relativePath;
        if (!std::filesystem::exists(fullPath)) {
            return "";
        }
        std::ifstream file(fullPath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Check if file exists
    bool FileExists(const std::string& relativePath) const {
        return std::filesystem::exists(m_tempDir / relativePath);
    }

    void Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
    }

private:
    std::filesystem::path m_tempDir;
};

// =============================================================================
// INI Content Helpers
// =============================================================================

// Count entries in an INI file content
inline size_t CountIniEntries(const std::string& content, const std::string& marker = "|posA(") {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find(marker, pos)) != std::string::npos) {
        count++;
        pos += marker.length();
    }
    return count;
}

// Check if INI content contains a specific form key
inline bool IniContainsFormKey(const std::string& content, const std::string& formKey) {
    return content.find(formKey + "|") != std::string::npos;
}

// Check if INI content contains the Initially Disabled flag (deleted entry)
inline bool IniContainsDeletedEntry(const std::string& content, const std::string& formKey) {
    size_t formKeyPos = content.find(formKey + "|");
    if (formKeyPos == std::string::npos) return false;

    size_t lineEnd = content.find('\n', formKeyPos);
    std::string line = content.substr(formKeyPos, lineEnd - formKeyPos);
    return line.find("flags(0x00000800)") != std::string::npos;
}

// Extract position from an INI line
inline bool ExtractPositionFromIni(const std::string& content, const std::string& formKey,
                                    float& x, float& y, float& z) {
    size_t formKeyPos = content.find(formKey + "|");
    if (formKeyPos == std::string::npos) return false;

    size_t posStart = content.find("posA(", formKeyPos);
    if (posStart == std::string::npos) return false;

    posStart += 5; // Skip "posA("
    size_t posEnd = content.find(")", posStart);
    if (posEnd == std::string::npos) return false;

    std::string posStr = content.substr(posStart, posEnd - posStart);
    return sscanf(posStr.c_str(), "%f,%f,%f", &x, &y, &z) == 3;
}

// =============================================================================
// Registry State Builders
// =============================================================================

// Build a registry with various object states for testing edge cases
class RegistryStateBuilder {
public:
    RegistryStateBuilder& WithModifiedObject(
        const std::string& formKey,
        const std::string& cellFormKey,
        float x, float y, float z,
        bool pendingExport = true)
    {
        auto saveData = MakeExistingSaveData(formKey, cellFormKey);
        Persistence::ChangedObjectRuntimeData data;
        data.saveData = saveData;
        data.currentTransform = MakeTransform(x, y, z);
        data.hasPendingExportChanges = pendingExport;
        data.createdThisSession = false;
        m_entries.push_back({formKey, std::move(data)});
        return *this;
    }

    RegistryStateBuilder& WithDeletedObject(
        const std::string& formKey,
        const std::string& cellFormKey,
        bool pendingExport = true)
    {
        auto saveData = MakeExistingSaveData(formKey, cellFormKey, "", true);
        Persistence::ChangedObjectRuntimeData data;
        data.saveData = saveData;
        data.currentTransform = MakeTransform(0, 0, 0);
        data.hasPendingExportChanges = pendingExport;
        data.createdThisSession = false;
        m_entries.push_back({formKey, std::move(data)});
        return *this;
    }

    RegistryStateBuilder& WithCreatedObject(
        const std::string& formKey,
        const std::string& baseFormKey,
        const std::string& cellFormKey,
        float x, float y, float z,
        bool pendingExport = true)
    {
        auto saveData = MakeCreatedSaveData(formKey, baseFormKey, cellFormKey);
        Persistence::ChangedObjectRuntimeData data;
        data.saveData = saveData;
        data.currentTransform = MakeTransform(x, y, z);
        data.hasPendingExportChanges = pendingExport;
        data.createdThisSession = true;
        data.firstChangeActionId = Util::UUID::Generate();
        m_entries.push_back({formKey, std::move(data)});
        return *this;
    }

    RegistryStateBuilder& WithNoCellInfo(
        const std::string& formKey,
        bool isCreated = false)
    {
        Persistence::ChangedObjectSaveGameData saveData;
        saveData.formKeyString = formKey;
        saveData.cellFormKey = "";  // Missing cell info!
        saveData.cellEditorId = "";
        saveData.wasCreated = isCreated;

        Persistence::ChangedObjectRuntimeData data;
        data.saveData = saveData;
        data.currentTransform = MakeTransform(50, 50, 50);
        data.hasPendingExportChanges = true;
        data.createdThisSession = true;
        m_entries.push_back({formKey, std::move(data)});
        return *this;
    }

    RegistryStateBuilder& WithEmptyBaseForm(
        const std::string& formKey,
        const std::string& cellFormKey)
    {
        auto saveData = MakeCreatedSaveData(formKey, "", cellFormKey);  // Empty base form!
        Persistence::ChangedObjectRuntimeData data;
        data.saveData = saveData;
        data.currentTransform = MakeTransform(100, 100, 100);
        data.hasPendingExportChanges = true;
        data.createdThisSession = true;
        m_entries.push_back({formKey, std::move(data)});
        return *this;
    }

    const std::vector<std::pair<std::string, Persistence::ChangedObjectRuntimeData>>& GetEntries() const {
        return m_entries;
    }

    // Convert to vector of pairs with const pointers (matching exporter API)
    std::vector<std::pair<std::string, const Persistence::ChangedObjectRuntimeData*>>
    GetEntriesAsConstPtrs() const {
        std::vector<std::pair<std::string, const Persistence::ChangedObjectRuntimeData*>> result;
        for (const auto& [key, data] : m_entries) {
            result.emplace_back(key, &data);
        }
        return result;
    }

private:
    std::vector<std::pair<std::string, Persistence::ChangedObjectRuntimeData>> m_entries;
};

} // namespace TestHelpers
