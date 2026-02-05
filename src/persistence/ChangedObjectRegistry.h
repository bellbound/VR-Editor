#pragma once

#include "../util/UUID.h"
#include <RE/N/NiTransform.h>
#include <RE/F/FormTypes.h>
#include <string>
#include <unordered_map>
#include <optional>
#include <vector>
#include <chrono>
#include <shared_mutex>

namespace Persistence {

// Data that gets serialized to save game
// This represents the original state of an object before any modifications
struct ChangedObjectSaveGameData {
    std::string formKeyString;           // "0x10C0E3~Skyrim.esm" - stable across sessions
    RE::NiTransform originalTransform;   // Transform before any modifications
    std::string baseFormKey;             // Base form key (for created/deleted objects)
    std::string cellFormKey;             // Parent cell FormKey (captured at registration time)
    std::string cellEditorId;            // Parent cell editor ID (captured at registration time)
    bool wasDeleted = false;             // True if object is currently "deleted"
    bool wasCreated = false;             // True if object was created by this mod (e.g., via copy)
    bool pendingHardDelete = false;       // True if dynamic ref needs SetDelete() on next load
    int64_t timestamp = 0;               // Unix timestamp (seconds since epoch) when first modified

    ChangedObjectSaveGameData() = default;
};

// Runtime data including non-serialized fields
// Contains the serializable data plus session-specific tracking
struct ChangedObjectRuntimeData {
    ChangedObjectSaveGameData saveData;
    Util::ActionId firstChangeActionId;  // The action that first changed this object
    bool createdThisSession = true;      // False if loaded from save game

    // ===== BOS Export Data =====
    // These fields track the CURRENT state for Base Object Swapper INI export
    RE::NiTransform currentTransform;    // Current transform (for BOS export)
    std::string locationName;            // Location name for INI file grouping
    bool hasPendingExportChanges = false;// True if currentTransform differs from last export

    ChangedObjectRuntimeData() = default;
};

// ChangedObjectRegistry: Singleton that tracks modified objects
//
// Purpose:
// - Stores the ORIGINAL state of objects before any modifications
// - Only records once per object (first modification)
// - Persists to save games via SaveGameDataManager
//
// Integration:
// - ActionHistoryRepository::Add() calls RegisterIfNew() when actions are created
// - UndoRedoController calls OnActionUndone() when actions are undone
// - SaveGameDataManager calls GetAllEntries()/LoadEntries()/Clear() for serialization
//
// Undo Behavior:
// - Only entries created this session can be removed on undo
// - Entries loaded from save games are permanent (no ActionId link)
class ChangedObjectRegistry {
public:
    static ChangedObjectRegistry* GetSingleton();

    // ========== Registration ==========

    // Register an object's original state before first modification
    // Called by ActionHistoryRepository::Add() when a new action is created
    // Only registers if the object is not already in the registry
    //
    // Parameters:
    //   ref: The reference being modified
    //   originalTransform: The transform BEFORE this modification
    //   actionId: The ID of the action that is changing this object
    void RegisterIfNew(RE::TESObjectREFR* ref,
                       const RE::NiTransform& originalTransform,
                       const Util::ActionId& actionId);

    // Register a deleted object
    // Called when DeleteAction is created
    // Stores additional baseFormId for potential object recreation
    void RegisterDeletedIfNew(RE::TESObjectREFR* ref,
                              RE::FormID baseFormId,
                              const RE::NiTransform& originalTransform,
                              const Util::ActionId& actionId);

    // Register a newly created object (e.g., from copy/duplicate)
    // These are objects that didn't exist before and were spawned by this mod
    // Parameters:
    //   ref: The newly created reference
    //   baseFormId: The base form used to create this reference
    //   transform: The initial transform of the created object
    //   actionId: The ID of the action that created this object
    void RegisterCreatedObject(RE::TESObjectREFR* ref,
                               RE::FormID baseFormId,
                               const RE::NiTransform& transform,
                               const Util::ActionId& actionId);

    // ========== Undo Integration ==========

    // Called when an action is undone
    // If the undone action's ID matches an entry's firstChangeActionId AND
    // the entry was created this session, removes the entry from registry
    //
    // This allows "full undo" - if user undoes all changes to an object
    // in a session, the object is removed from the changed objects list
    void OnActionUndone(const Util::ActionId& undoneActionId);

    // ========== Deferred Hard Delete (for Dynamic Refs) ==========

    // Mark a dynamic object for hard deletion on next load
    // Called when deleting dynamic refs (copies/gallery spawns)
    // The actual SetDelete() is deferred to PostLoadGame for safety
    void MarkPendingHardDelete(const std::string& formKey);

    // Process all pending hard deletes
    // Called from PostLoadGame - calls SetDelete(true) on marked dynamic refs
    void ProcessPendingHardDeletes();

    // ========== Transform Updates (for BOS Export) ==========

    // Update the current transform of an object
    // Marks the entry as having pending export changes
    // Also updates the location name for INI file grouping
    void UpdateCurrentTransform(const std::string& formKey,
                                const RE::NiTransform& currentTransform,
                                std::string_view locationName);

    // Get all entries that have pending export changes
    // Used by BaseObjectSwapperExporter to determine what to write
    std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>> GetPendingExportEntries() const;

    // Clear pending export flags for non-created objects (called after BOS export)
    void ClearPendingExportFlags();

    // Clear pending export flags for created objects only (called after AddedObjects export)
    void ClearPendingExportFlagsForCreatedObjects();

    // ========== Query ==========

    // Get the original state of an object if it exists in registry
    std::optional<ChangedObjectSaveGameData> GetOriginalState(const std::string& formKey) const;

    // Check if an object is in the registry
    bool Contains(const std::string& formKey) const;

    // Get count of entries
    size_t Count() const;

    // Extract and remove all entries for a specific cell FormKey
    std::vector<std::pair<std::string, ChangedObjectRuntimeData>> ExtractEntriesForCell(
        const std::string& cellFormKey);

    // ========== Serialization Support ==========

    // Get all entries for serialization
    const std::unordered_map<std::string, ChangedObjectRuntimeData>& GetAllEntries() const;

    // Load entries from deserialized data (called by SaveGameDataManager)
    // Loaded entries have createdThisSession=false and invalid ActionId
    void LoadEntries(std::vector<ChangedObjectSaveGameData>&& entries);

    // Clear all entries (called on game revert)
    void Clear();

private:
    ChangedObjectRegistry() = default;
    ~ChangedObjectRegistry() = default;
    ChangedObjectRegistry(const ChangedObjectRegistry&) = delete;
    ChangedObjectRegistry& operator=(const ChangedObjectRegistry&) = delete;

    // Map of formKey -> runtime data
    // Key is the stable form key string (e.g., "0x10C0E3~Skyrim.esm")
    std::unordered_map<std::string, ChangedObjectRuntimeData> m_entries;

    // Thread safety: SKSE serialization callbacks run on different threads
    // Uses shared_mutex for read-heavy workload (many queries, fewer writes)
    mutable std::shared_mutex m_mutex;
};

} // namespace Persistence
