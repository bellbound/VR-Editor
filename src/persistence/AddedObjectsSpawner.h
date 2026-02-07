#pragma once

#include "AddedObjectsParser.h"
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>
#include <unordered_set>
#include <unordered_map>
#include <shared_mutex>
#include <vector>

namespace Persistence {

// AddedObjectsSpawner: Handles spawning dynamic objects when player enters cells
// 
// Important: This is currently UNUSED! Added Items are automatically persistet in the save.  
//
// Purpose:
// - Load and cache all _AddedObjects.ini files on startup
// - Listen for cell enter events
// - Spawn objects from INI files when player enters a cell
// - Track which objects have been spawned this session (to prevent duplicates)
//
// Session Tracking:
// - Objects are spawned once per game session
// - Spawned objects persist until game is closed
// - Objects are NOT re-spawned when loading a save (they persist from the session)
// - The m_spawnedThisSession set tracks unique entry IDs to prevent duplicates
//
// Integration:
// - Initialize() is called from plugin.cpp on DataLoaded
// - OnCellEnter() is called from cell attach event sink
class AddedObjectsSpawner {
public:
    static AddedObjectsSpawner* GetSingleton();

    // ========== Lifecycle ==========

    // Initialize the spawner
    // - Scans Data folder for all _AddedObjects.ini files
    // - Parses and caches entries indexed by cell FormKey
    // - Should be called once during DataLoaded
    void Initialize();

    // Reset spawn tracking (called on new game or game revert)
    // This allows objects to be spawned again
    void ResetSpawnTracking();

    // ========== Cell Events ==========

    // Called when player enters a cell
    // - Looks up cell FormKey in cached entries
    // - Spawns any entries that haven't been spawned this session
    void OnCellEnter(RE::TESObjectCELL* cell);

    // Remove cached entries for a cell and clear spawn tracking for them
    // Returns number of entries removed
    size_t RemoveCellEntries(const std::string& cellFormKey);

    // ========== Query ==========

    // Check if an entry has been spawned this session
    bool HasBeenSpawnedThisSession(const std::string& entryId) const;

    // Get number of cached cells with entries
    size_t GetCachedCellCount() const;

    // Get total number of cached entries across all cells
    size_t GetTotalEntryCount() const;

    // ========== Manual Spawn ==========

    // Manually spawn an object (for testing or external use)
    // Returns the spawned reference, or nullptr on failure
    RE::TESObjectREFR* SpawnObject(const AddedObjectEntry& entry, RE::TESObjectCELL* cell);

private:
    AddedObjectsSpawner() = default;
    ~AddedObjectsSpawner() = default;
    AddedObjectsSpawner(const AddedObjectsSpawner&) = delete;
    AddedObjectsSpawner& operator=(const AddedObjectsSpawner&) = delete;

    // Generate a unique ID for an entry
    // Used for tracking spawned entries
    static std::string GenerateEntryId(const std::string& cellFormKey, const AddedObjectEntry& entry);

    // Spawn all entries for a cell that haven't been spawned yet
    void SpawnEntriesForCell(const std::string& cellFormKey, RE::TESObjectCELL* cell);

    // Mark an entry as spawned this session
    void MarkAsSpawned(const std::string& entryId);

    // ========== Data ==========

    // Cache of parsed INI data indexed by cell FormKey
    // One file = one cell, so this maps cellFormKey -> file data
    std::unordered_map<std::string, AddedObjectsFileData> m_filesByCell;

    // Set of entry IDs that have been spawned this session
    // Prevents duplicate spawning
    std::unordered_set<std::string> m_spawnedThisSession;

    // Thread safety
    mutable std::shared_mutex m_mutex;

    // Initialization flag
    bool m_initialized = false;
};

} // namespace Persistence
