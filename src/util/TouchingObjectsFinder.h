#pragma once

#include <RE/Skyrim.h>
#include <vector>
#include <unordered_set>

namespace Util {

// TouchingObjectsFinder: Finds physics objects that are in contact with (or sitting on)
// a set of selected objects, using AABB (Axis-Aligned Bounding Box) overlap detection.
//
// Use case: When grabbing a table, automatically include plates/cups sitting on it
//
class TouchingObjectsFinder
{
public:
    struct Config {
        // How much to expand the AABB beyond the object's bounds (in game units)
        // This accounts for objects barely touching or sitting on edges
        float aabbExpansion = 5.0f;

        // Maximum search radius from selection center (optimization)
        float maxSearchRadius = 500.0f;

        // Whether to include only "clutter" type objects (plates, cups, etc.)
        bool clutterOnly = true;

        // Include "props" collision layer as well
        bool includeProps = true;

        // Maximum number of touching objects to add (prevent runaway)
        size_t maxTouchingObjects = 50;
    };

    // Find all objects that are touching/sitting on the given selection
    // Returns objects NOT in the original selection that should be added
    static std::vector<RE::TESObjectREFR*> FindTouchingObjects(
        const std::vector<RE::TESObjectREFR*>& selection,
        const Config& config = Config{});

    // Simplified version: Add touching objects directly to selection
    // Returns the number of objects added
    static size_t AddTouchingObjectsToSelection(
        const std::vector<RE::TESObjectREFR*>& currentSelection,
        const Config& config = Config{});

private:
    // AABB structure for overlap testing
    struct WorldAABB {
        RE::NiPoint3 min;
        RE::NiPoint3 max;

        bool Overlaps(const WorldAABB& other) const;
        void Expand(float amount);
        RE::NiPoint3 Center() const;
    };

    // Get the world-space AABB of an object
    static bool GetWorldAABB(RE::TESObjectREFR* ref, WorldAABB& outAABB);

    // Check if an object's collision layer is valid for inclusion
    static bool IsValidCollisionLayer(RE::TESObjectREFR* ref, const Config& config);

    // Check if an object is moveable (has physics, not static architecture)
    static bool IsMoveable(RE::TESObjectREFR* ref);
};

} // namespace Util
