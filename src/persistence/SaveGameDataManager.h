#pragma once

#include <SKSE/SKSE.h>

namespace Persistence {

// SaveGameDataManager: Handles SKSE serialization callbacks
//
// Purpose:
// - Registers with SKSE's serialization interface
// - Saves ChangedObjectRegistry data when game is saved
// - Loads ChangedObjectRegistry data when game is loaded
// - Clears ChangedObjectRegistry on game revert (new game/load)
//
// Data Format (binary, SKSE cosave):
// - Record type: 'IGPV' (InGamePatcherVR)
// - Version: 1
// - Content: Array of ChangedObjectSaveGameData entries
class SaveGameDataManager {
public:
    static SaveGameDataManager* GetSingleton();

    // Register with SKSE serialization interface
    // Called during plugin load (SKSEPluginLoad)
    void Initialize(const SKSE::SerializationInterface* serialization);

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

private:
    SaveGameDataManager() = default;
    ~SaveGameDataManager() = default;
    SaveGameDataManager(const SaveGameDataManager&) = delete;
    SaveGameDataManager& operator=(const SaveGameDataManager&) = delete;

    // SKSE serialization callbacks
    static void OnSave(SKSE::SerializationInterface* intfc);
    static void OnLoad(SKSE::SerializationInterface* intfc);
    static void OnRevert(SKSE::SerializationInterface* intfc);

    // Serialization helpers
    static bool WriteString(SKSE::SerializationInterface* intfc, const std::string& str);
    static bool ReadString(SKSE::SerializationInterface* intfc, std::string& str);
    static bool WriteTransform(SKSE::SerializationInterface* intfc, const RE::NiTransform& transform);
    static bool ReadTransform(SKSE::SerializationInterface* intfc, RE::NiTransform& transform);

    // Record type identifier (4-byte "type" for SKSE)
    // 'IGPV' = InGamePatcherVR
    static constexpr uint32_t kRecordType = 'VGPI';  // Reversed for little-endian: "IGPV"
    static constexpr uint32_t kDataVersion = 3;      // v3: Added cellFormKey and cellEditorId fields

    // Gallery record type: 'GALY'
    static constexpr uint32_t kGalleryRecordType = 'YLAG';  // Reversed for little-endian: "GALY"
    static constexpr uint32_t kGalleryDataVersion = 3;      // v3: Added originalScale (v2 migrates to 1.0)

    // Safety limits to prevent crashes from corrupted save data
    static constexpr uint32_t kMaxStringLength = 1024;   // Form keys are short (e.g., "0x10C0E3~Skyrim.esm")
    static constexpr uint32_t kMaxEntryCount = 10000;    // Sanity limit for entry count

    bool m_initialized = false;
};

} // namespace Persistence
