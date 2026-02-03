#include "GalleryPlacementUtil.h"
#include "../persistence/FormKeyUtil.h"
#include "../log.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESForm.h>
#include <RE/M/Misc.h>
#include <cmath>
#include <algorithm>

namespace Gallery {

RE::TESObjectREFR* GalleryPlacementUtil::PlaceInFrontOfPlayer(const GalleryItem& item)
{
    // Resolve the base form key to a runtime FormID
    RE::FormID runtimeFormId = Persistence::FormKeyUtil::ResolveToRuntimeFormID(item.baseFormKey);
    if (runtimeFormId == 0) {
        spdlog::error("GalleryPlacementUtil::PlaceInFrontOfPlayer - failed to resolve form key: {}", item.baseFormKey);
        return nullptr;
    }

    // Look up the form
    auto* form = RE::TESForm::LookupByID(runtimeFormId);
    if (!form) {
        spdlog::error("GalleryPlacementUtil::PlaceInFrontOfPlayer - form not found for ID {:08X}", runtimeFormId);
        return nullptr;
    }

    // Cast to bound object (required for placement)
    auto* boundObj = form->As<RE::TESBoundObject>();
    if (!boundObj) {
        spdlog::error("GalleryPlacementUtil::PlaceInFrontOfPlayer - form is not a TESBoundObject");
        return nullptr;
    }

    // Get player
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("GalleryPlacementUtil::PlaceInFrontOfPlayer - player not available");
        return nullptr;
    }

    // Calculate spawn position
    RE::NiPoint3 playerPos = player->GetPosition();
    float yaw = player->GetAngleZ();

    // Forward vector in Skyrim's coordinate system (Z is up)
    RE::NiPoint3 forward(std::sin(yaw), std::cos(yaw), 0.0f);

    // Spawn at fixed distance in front of player
    // AABB offset disabled - objects can be repositioned after spawning
    // float aabbOffset = CalculateAABBOffset(boundObj);
    // float totalDistance = BASE_SPAWN_DISTANCE + aabbOffset;
    float totalDistance = BASE_SPAWN_DISTANCE;

    RE::NiPoint3 spawnPos = playerPos + (forward * totalDistance);

    // Place the object (persist=true so game handles save/load)
    auto newRefPtr = player->PlaceObjectAtMe(boundObj, true);
    RE::TESObjectREFR* newRef = newRefPtr.get();
    if (!newRef) {
        spdlog::error("GalleryPlacementUtil::PlaceInFrontOfPlayer - PlaceObjectAtMe failed");
        return nullptr;
    }

    // Set the actual position
    newRef->SetPosition(spawnPos);

    // Reset rotation to default (0, 0, 0) - objects can spawn with weird orientations otherwise
    newRef->SetAngle(RE::NiPoint3(0.0f, 0.0f, 0.0f));

    // Apply the original scale from when the object was saved to gallery
    if (item.originalScale > 0.0f && item.originalScale != 1.0f) {
        // refScale is stored as percentage (1.0 = 100%)
        newRef->GetReferenceRuntimeData().refScale = static_cast<uint16_t>(item.originalScale * 100.0f);
    }

    newRef->Update3DPosition(true);

    spdlog::info("GalleryPlacementUtil::PlaceInFrontOfPlayer - placed '{}' at ({:.1f}, {:.1f}, {:.1f}), "
        "distance: {:.1f}, scale: {:.2f}",
        item.displayName, spawnPos.x, spawnPos.y, spawnPos.z,
        totalDistance, item.originalScale);

    // Debug notification
    std::string notification = "Spawned " + item.displayName;
    RE::DebugNotification(notification.c_str());

    return newRef;
}

float GalleryPlacementUtil::CalculateAABBOffset(RE::TESBoundObject* boundObj)
{
    if (!boundObj) {
        return 0.0f;
    }

    auto& bd = boundObj->boundData;

    // Calculate horizontal dimensions (X and Y - Z is up in Skyrim)
    float sizeX = static_cast<float>(bd.boundMax.x - bd.boundMin.x);
    float sizeY = static_cast<float>(bd.boundMax.y - bd.boundMin.y);

    // Use the larger horizontal dimension since we don't know the object's orientation
    // relative to the player's facing direction
    float maxHorizontal = (std::max)(sizeX, sizeY);

    // Return half the extent - this pushes the object's center far enough
    // that its nearest edge is roughly at the base spawn distance
    float offset = maxHorizontal / 2.0f;

    spdlog::debug("GalleryPlacementUtil::CalculateAABBOffset - bounds X: {:.1f}, Y: {:.1f}, offset: {:.1f}",
        sizeX, sizeY, offset);

    return offset;
}

} // namespace Gallery
