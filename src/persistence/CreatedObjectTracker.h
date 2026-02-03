#pragma once

#include <RE/Skyrim.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

namespace Persistence {

// Data stored for each created object
// Objects are created with forcePersist=true so the game handles save/load
struct TrackedCreatedObject {
    RE::FormID baseFormId = 0;        // Base object for spawning
    std::string cellFormKey;          // Which cell it belongs to
    RE::NiPoint3 position;            // World position
    RE::NiPoint3 rotation;            // Rotation in degrees (for spawning)
    float scale = 1.0f;               // Object scale

    // Transient ref handle - safely tracks the object while it exists in world
    // Uses handle system to avoid dangling pointers when engine deletes refs
    // Used for deletion, NOT for persistence
    RE::ObjectRefHandle currentRefHandle;

    // Generate unique key for deduplication (position-based)
    std::string GetUniqueKey() const;
};

// CreatedObjectTracker: Tracks dynamically created objects
//
// Purpose:
// - Track objects created during session (copies, gallery spawns, INI spawns)
// - Objects are created with forcePersist=true so the game handles save/load
//
// Note: This is separate from ChangedObjectRegistry which handles INI export.
class CreatedObjectTracker {
public:
    static CreatedObjectTracker* GetSingleton();

    // ========== Object Tracking ==========

    // Add a created object to tracking
    // Called by CopyHandler, Gallery, AddedObjectsSpawner after creating a ref
    void Add(RE::TESObjectREFR* ref, RE::FormID baseFormId, const std::string& cellFormKey);

    // Remove an object from tracking (e.g., when deleted by user)
    // Matches by currentRefHandle
    void Remove(RE::TESObjectREFR* ref);

    // Remove by unique key (position-based)
    void RemoveByKey(const std::string& key);

    // Check if an object at this position is already tracked
    bool IsTracked(const std::string& cellFormKey, const RE::NiPoint3& position) const;

    // ========== Save Hooks (legacy, now no-ops) ==========

    // No-op: game handles persistence via forcePersist=true
    std::string OnPreSave();

    // No-op: game handles persistence via forcePersist=true
    void OnPostSave(const std::string& playerCellFormKey);

    // ========== Cell Events ==========

    // Spawn all tracked objects for a cell (called on cell enter)
    void SpawnForCell(const std::string& cellFormKey, RE::TESObjectCELL* cell);

    // Delete all tracked objects for a cell (called on cell leave)
    void DeleteForCell(const std::string& cellFormKey);

    // Delete and remove all tracked objects for a cell
    // Returns number of entries removed
    size_t RemoveForCell(const std::string& cellFormKey);

    // ========== Query ==========

    // Get count of tracked objects
    size_t GetCount() const;

    // Get count of objects in a specific cell
    size_t GetCountForCell(const std::string& cellFormKey) const;

    // ========== Lifecycle ==========

    // Clear all tracking (called on game revert/new game)
    void Clear();

private:
    CreatedObjectTracker() = default;
    ~CreatedObjectTracker() = default;
    CreatedObjectTracker(const CreatedObjectTracker&) = delete;
    CreatedObjectTracker& operator=(const CreatedObjectTracker&) = delete;

    // Spawn a single object, returns the new ref
    RE::TESObjectREFR* SpawnObject(TrackedCreatedObject& obj, RE::TESObjectCELL* cell);

    // Delete a single object from the world
    void DeleteObject(TrackedCreatedObject& obj);

    // Get cell from FormKey
    RE::TESObjectCELL* ResolveCellFromFormKey(const std::string& cellFormKey) const;

    // ========== Data ==========

    // All tracked objects, indexed by cell FormKey for fast lookup
    std::unordered_map<std::string, std::vector<TrackedCreatedObject>> m_objectsByCell;

    // Thread safety
    mutable std::shared_mutex m_mutex;
};

} // namespace Persistence
