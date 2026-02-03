#include "TouchingObjectsFinder.h"
#include "../selection/SelectionState.h"
#include "../log.h"
#include <cmath>
#include <algorithm>

namespace Util {

bool TouchingObjectsFinder::WorldAABB::Overlaps(const WorldAABB& other) const
{
    // Two AABBs overlap if they overlap on all three axes
    // They DON'T overlap if separated on any axis
    if (max.x < other.min.x || min.x > other.max.x) return false;
    if (max.y < other.min.y || min.y > other.max.y) return false;
    if (max.z < other.min.z || min.z > other.max.z) return false;
    return true;
}

void TouchingObjectsFinder::WorldAABB::Expand(float amount)
{
    min.x -= amount;
    min.y -= amount;
    min.z -= amount;
    max.x += amount;
    max.y += amount;
    max.z += amount;
}

RE::NiPoint3 TouchingObjectsFinder::WorldAABB::Center() const
{
    return RE::NiPoint3{
        (min.x + max.x) * 0.5f,
        (min.y + max.y) * 0.5f,
        (min.z + max.z) * 0.5f
    };
}

bool TouchingObjectsFinder::GetWorldAABB(RE::TESObjectREFR* ref, WorldAABB& outAABB)
{
    if (!ref) {
        return false;
    }

    auto* node3D = ref->Get3D();
    if (!node3D) {
        return false;
    }

    // Try to get AABB from physics collision object (most accurate)
    auto* collisionObj = node3D->GetCollisionObject();
    if (collisionObj) {
        auto* rigidBody = collisionObj->GetRigidBody();
        if (rigidBody) {
            RE::hkAabb havokAABB;
            rigidBody->GetAabbWorldspace(havokAABB);

            // Convert from Havok scale to Skyrim world scale
            // Havok uses ~1/69.99 scale factor
            float worldScale = RE::bhkWorld::GetWorldScaleInverse();

            outAABB.min.x = havokAABB.min.quad.m128_f32[0] * worldScale;
            outAABB.min.y = havokAABB.min.quad.m128_f32[1] * worldScale;
            outAABB.min.z = havokAABB.min.quad.m128_f32[2] * worldScale;
            outAABB.max.x = havokAABB.max.quad.m128_f32[0] * worldScale;
            outAABB.max.y = havokAABB.max.quad.m128_f32[1] * worldScale;
            outAABB.max.z = havokAABB.max.quad.m128_f32[2] * worldScale;

            return true;
        }
    }

    // Fallback: use the NiBound (bounding sphere) to create an AABB
    const auto& worldBound = node3D->worldBound;
    float radius = worldBound.radius;

    if (radius > 0.0f) {
        RE::NiPoint3 center = worldBound.center;

        outAABB.min.x = center.x - radius;
        outAABB.min.y = center.y - radius;
        outAABB.min.z = center.z - radius;
        outAABB.max.x = center.x + radius;
        outAABB.max.y = center.y + radius;
        outAABB.max.z = center.z + radius;

        return true;
    }

    // Last resort: use position with a small default size
    RE::NiPoint3 pos = ref->GetPosition();
    constexpr float defaultHalfSize = 20.0f;

    outAABB.min.x = pos.x - defaultHalfSize;
    outAABB.min.y = pos.y - defaultHalfSize;
    outAABB.min.z = pos.z - defaultHalfSize;
    outAABB.max.x = pos.x + defaultHalfSize;
    outAABB.max.y = pos.y + defaultHalfSize;
    outAABB.max.z = pos.z + defaultHalfSize;

    return true;
}

bool TouchingObjectsFinder::IsValidCollisionLayer(RE::TESObjectREFR* ref, const Config& config)
{
    auto* node3D = ref->Get3D();
    if (!node3D) {
        return false;
    }

    RE::COL_LAYER layer = node3D->GetCollisionLayer();

    // Clutter is the primary target (plates, cups, small items)
    if (layer == RE::COL_LAYER::kClutter) {
        return true;
    }

    // Props are larger moveable items
    if (config.includeProps && layer == RE::COL_LAYER::kProps) {
        return true;
    }

    // Also include weapons (swords on tables, etc.)
    if (layer == RE::COL_LAYER::kWeapon) {
        return true;
    }

    // Small debris
    if (layer == RE::COL_LAYER::kDebrisSmall) {
        return true;
    }

    // If clutterOnly is false, accept a broader range
    if (!config.clutterOnly) {
        // AnimStatic - animated static objects that might be moveable
        if (layer == RE::COL_LAYER::kAnimStatic) {
            return true;
        }
    }

    return false;
}

bool TouchingObjectsFinder::IsMoveable(RE::TESObjectREFR* ref)
{
    if (!ref || !ref->Get3D()) {
        return false;
    }

    // Ignore the player and NPCs
    if (ref->As<RE::Actor>()) {
        return false;
    }

    // Check if it's a static form that shouldn't move
    auto* baseObj = ref->GetBaseObject();
    if (baseObj) {
        // Skip doors
        if (baseObj->Is(RE::FormType::Door)) {
            return false;
        }

        // Skip containers (chests, barrels) - they usually shouldn't auto-include
        if (baseObj->Is(RE::FormType::Container)) {
            return false;
        }

        // Skip activators (levers, buttons, etc.)
        if (baseObj->Is(RE::FormType::Activator)) {
            return false;
        }

        // STAT (static objects) - architecture, shouldn't move
        if (baseObj->Is(RE::FormType::Static)) {
            return false;
        }
    }

    // Check for collision object (physics-enabled)
    auto* collisionObj = ref->Get3D()->GetCollisionObject();
    if (!collisionObj) {
        return false;  // No physics = probably not moveable
    }

    return true;
}

std::vector<RE::TESObjectREFR*> TouchingObjectsFinder::FindTouchingObjects(
    const std::vector<RE::TESObjectREFR*>& selection,
    const Config& config)
{
    std::vector<RE::TESObjectREFR*> result;

    if (selection.empty()) {
        return result;
    }

    // Build set of already-selected FormIDs for quick lookup
    std::unordered_set<RE::FormID> selectedFormIds;
    for (auto* ref : selection) {
        if (ref) {
            selectedFormIds.insert(ref->GetFormID());
        }
    }

    // Get AABBs for all selected objects (expanded for touching detection)
    std::vector<WorldAABB> selectionAABBs;
    selectionAABBs.reserve(selection.size());

    RE::NiPoint3 selectionCenter{0, 0, 0};
    int validCount = 0;

    for (auto* ref : selection) {
        WorldAABB aabb;
        if (GetWorldAABB(ref, aabb)) {
            aabb.Expand(config.aabbExpansion);
            selectionAABBs.push_back(aabb);

            auto center = aabb.Center();
            selectionCenter.x += center.x;
            selectionCenter.y += center.y;
            selectionCenter.z += center.z;
            validCount++;
        }
    }

    if (selectionAABBs.empty()) {
        spdlog::warn("TouchingObjectsFinder: No valid AABBs in selection");
        return result;
    }

    // Calculate average center
    selectionCenter.x /= validCount;
    selectionCenter.y /= validCount;
    selectionCenter.z /= validCount;

    // Get the cell to search
    RE::TESObjectREFR* firstRef = selection[0];
    auto* cell = firstRef ? firstRef->GetParentCell() : nullptr;

    if (!cell) {
        spdlog::warn("TouchingObjectsFinder: Could not get parent cell");
        return result;
    }

    // Iterate through references in range
    cell->ForEachReferenceInRange(selectionCenter, config.maxSearchRadius,
        [&](RE::TESObjectREFR* candidate) -> RE::BSContainer::ForEachResult {

            // Skip if we've hit the max
            if (result.size() >= config.maxTouchingObjects) {
                return RE::BSContainer::ForEachResult::kStop;
            }

            // Skip if null or already selected
            if (!candidate || selectedFormIds.contains(candidate->GetFormID())) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Skip if no 3D representation
            if (!candidate->Get3D()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Skip deleted/disabled refs
            if (candidate->IsDeleted() || candidate->IsDisabled()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check if it's a moveable type
            if (!IsMoveable(candidate)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check collision layer
            if (!IsValidCollisionLayer(candidate, config)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Get candidate's AABB
            WorldAABB candidateAABB;
            if (!GetWorldAABB(candidate, candidateAABB)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check if it overlaps with any selected object
            for (const auto& selAABB : selectionAABBs) {
                if (candidateAABB.Overlaps(selAABB)) {
                    result.push_back(candidate);
                    spdlog::trace("TouchingObjectsFinder: Found touching object {:08X} ({})",
                        candidate->GetFormID(),
                        candidate->GetBaseObject() ? candidate->GetBaseObject()->GetName() : "unknown");
                    break;  // Only add once
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

    spdlog::info("TouchingObjectsFinder: Found {} touching objects for {} selected objects",
        result.size(), selection.size());

    return result;
}

size_t TouchingObjectsFinder::AddTouchingObjectsToSelection(
    const std::vector<RE::TESObjectREFR*>& currentSelection,
    const Config& config)
{
    auto touchingObjects = FindTouchingObjects(currentSelection, config);

    if (touchingObjects.empty()) {
        return 0;
    }

    auto* selectionState = Selection::SelectionState::GetSingleton();

    for (auto* obj : touchingObjects) {
        selectionState->AddToSelection(obj);
    }

    spdlog::info("TouchingObjectsFinder: Added {} touching objects to selection", touchingObjects.size());

    return touchingObjects.size();
}

} // namespace Util
