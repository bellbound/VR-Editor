#pragma once

#include "GalleryItem.h"
#include <RE/Skyrim.h>
#include <vector>
#include <string>

namespace Gallery {

// GalleryManager: Singleton managing the user's saved object gallery
//
// Purpose:
// - Stores base objects that users want to reuse
// - Persists gallery items via SKSE cosave (handled by SaveGameDataManager)
// - Provides PlaceObject to spawn gallery items in the world
//
// Objects are identified by mesh path, so any object with the same mesh
// is considered "in gallery" regardless of which specific instance was saved
class GalleryManager {
public:
    static GalleryManager* GetSingleton();

    // Get all gallery items (unsorted, insertion order)
    const std::vector<GalleryItem>& GetObjects() const { return m_items; }

    // Get gallery items sorted for display:
    // - Grouped by source plugin
    // - Within groups: newest first (by timestamp)
    // - Groups ordered by their newest item's timestamp (newest first)
    std::vector<GalleryItem> GetSortedObjects() const;

    // Get item count
    size_t GetCount() const { return m_items.size(); }

    // Check if gallery is empty
    bool IsEmpty() const { return m_items.empty(); }

    // Add an object's base form to the gallery
    // Returns true if added, false if already exists or invalid
    bool AddObject(RE::TESObjectREFR* ref);

    // Remove an object from gallery by its mesh path
    // Returns true if removed, false if not found
    bool RemoveObject(const std::string& meshPath);

    // Check if an object is in the gallery (by mesh path)
    bool IsInGallery(RE::TESObjectREFR* ref) const;
    bool IsInGalleryByMesh(const std::string& meshPath) const;

    // Get the mesh path for a reference
    // Returns empty string if ref is null or has no valid mesh
    std::string GetMeshPath(RE::TESObjectREFR* ref) const;

    // Place a gallery item in the world (300 units in front of player)
    // Returns the newly created reference, or nullptr on failure
    RE::TESObjectREFR* PlaceObject(const GalleryItem& item);

    // Clear all gallery items (called on game revert)
    void Clear();

    // Load entries from save data (called by SaveGameDataManager)
    void LoadEntries(std::vector<GalleryItem> items);

private:
    GalleryManager() = default;
    ~GalleryManager() = default;
    GalleryManager(const GalleryManager&) = delete;
    GalleryManager& operator=(const GalleryManager&) = delete;

    // Find item index by mesh path, returns -1 if not found
    int FindItemIndexByMesh(const std::string& meshPath) const;

    // Extract plugin name from baseFormKey (e.g., "0x10C0E3~Skyrim.esm" -> "Skyrim.esm")
    std::string ExtractPluginName(const std::string& baseFormKey) const;

    // Extract display name from a base object
    std::string ExtractDisplayName(RE::TESBoundObject* baseObj) const;

    // Calculate target scale for gallery UI based on bounding box
    // Adjusts for the reference's current scale
    float CalculateTargetScale(RE::TESBoundObject* baseObj, float refScale) const;

    std::vector<GalleryItem> m_items;
};

} // namespace Gallery
