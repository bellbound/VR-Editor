#pragma once

#include <string>
#include <cstdint>

namespace Gallery {

// Represents a saved object in the user's gallery
// Objects are identified by their mesh path, so any object with the same mesh
// is considered "in gallery" regardless of which specific reference was saved
struct GalleryItem {
    std::string meshPath;        // Primary key - identifies objects by mesh (e.g., "meshes\\clutter\\bucket.nif")
    std::string baseFormKey;     // Load-order safe form key for spawning: "0x12AB~Skyrim.esm"
    std::string displayName;     // Cached name for UI tooltip
    float targetScale;           // Pre-calculated scale to fit in UI (based on bounding box)
    float originalScale;         // Scale of the object when saved to gallery (applied when spawning)
    uint64_t addedTimestamp;     // When added to gallery (seconds since epoch)

    GalleryItem() : targetScale(1.0f), originalScale(1.0f), addedTimestamp(0) {}

    GalleryItem(std::string mesh, std::string formKey, std::string name, float tgtScale, float origScale, uint64_t timestamp)
        : meshPath(std::move(mesh))
        , baseFormKey(std::move(formKey))
        , displayName(std::move(name))
        , targetScale(tgtScale)
        , originalScale(origScale)
        , addedTimestamp(timestamp)
    {}
};

} // namespace Gallery
