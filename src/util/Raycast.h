#pragma once

#include "RE/Skyrim.h"

// Layer mask for collision layer filtering (bitmask of COL_LAYER values)
using CollisionLayerMask = uint64_t;

// Helper to create a layer mask from a single layer
constexpr CollisionLayerMask MakeLayerMask(RE::COL_LAYER layer) {
    return 1ULL << static_cast<uint64_t>(layer);
}

// Combine multiple masks with bitwise OR
template<typename... Layers>
constexpr CollisionLayerMask MakeLayerMask(RE::COL_LAYER first, Layers... rest) {
    return MakeLayerMask(first) | MakeLayerMask(rest...);
}

// Pre-defined masks for common use cases
namespace LayerMasks {
    // Solid world geometry - walls, floors, terrain (excludes trees)
    constexpr CollisionLayerMask kSolidNoTrees =
        MakeLayerMask(RE::COL_LAYER::kStatic) |
        MakeLayerMask(RE::COL_LAYER::kAnimStatic) |
        MakeLayerMask(RE::COL_LAYER::kTerrain) |
        MakeLayerMask(RE::COL_LAYER::kGround) |
        MakeLayerMask(RE::COL_LAYER::kCollisionBox) |
        MakeLayerMask(RE::COL_LAYER::kProps);

    // Solid geometry including trees
    constexpr CollisionLayerMask kSolid = kSolidNoTrees |
        MakeLayerMask(RE::COL_LAYER::kTrees);

    // Clutter layers (grabbable items)
    constexpr CollisionLayerMask kClutter =
        MakeLayerMask(RE::COL_LAYER::kClutter) |
        MakeLayerMask(RE::COL_LAYER::kClutterLarge) |
        MakeLayerMask(RE::COL_LAYER::kDebrisSmall) |
        MakeLayerMask(RE::COL_LAYER::kDebrisLarge) |
        MakeLayerMask(RE::COL_LAYER::kWeapon);
}

struct RaycastResult {
    bool hit;
    float distance;
    RE::NiPoint3 origin;           // The origin point of the ray
    RE::NiPoint3 hitPoint;
    RE::NiPoint3 hitNormal;
    RE::COL_LAYER collisionLayer;  // Layer of the hit object (only valid if hit == true)
    RE::TESObjectREFR* hitRef;     // The object reference that was hit (may be nullptr)
};

namespace Raycast {
    // Layer filter function type - returns true if the layer should block movement
    using LayerFilter = bool(*)(RE::COL_LAYER);

    // Cast a ray from origin in direction, returns hit info
    // maxDistance is in game units
    RaycastResult CastRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance);

    // Cast a ray with Havok filterInfo set to a specific source layer
    // sourceLayer: which layer the ray "acts as" for collision filtering (e.g., kLOS to bypass trees)
    RaycastResult CastRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, RE::COL_LAYER sourceLayer);

    // Cast a ray that only hits layers matching the given mask
    // Uses iterative raycasting to skip non-matching layers
    // layerMask: bitmask of acceptable layers (use LayerMasks::kSolid or MakeLayerMask())
    RaycastResult CastRayFiltered(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, CollisionLayerMask layerMask);

    // Check if movement in a direction is blocked by geometry
    // Returns the allowed distance (clamped to maxDistance if no obstacle, or distance to wall minus buffer)
    float GetAllowedDistance(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, float buffer);

    // Same as above, but only considers hits on layers that pass the filter
    // layerFilter should return true for layers that should block movement
    float GetAllowedDistance(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, float buffer, LayerFilter layerFilter);
}
