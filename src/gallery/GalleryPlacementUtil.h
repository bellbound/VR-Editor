#pragma once

#include "GalleryItem.h"
#include <RE/T/TESObjectREFR.h>

namespace Gallery {

class GalleryPlacementUtil {
public:
    // Places a gallery item in front of the player, offset by AABB to prevent
    // large objects from swallowing the player
    static RE::TESObjectREFR* PlaceInFrontOfPlayer(const GalleryItem& item);

    // Base distance in front of player (before AABB offset)
    static constexpr float BASE_SPAWN_DISTANCE = 300.0f;

private:
    // Calculate additional offset based on object's AABB along the forward axis
    // Returns half the object's extent in the horizontal plane (max of X, Y bounds)
    // since object orientation relative to player is unknown
    static float CalculateAABBOffset(RE::TESBoundObject* boundObj);
};

} // namespace Gallery
