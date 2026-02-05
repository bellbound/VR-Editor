#include "ObjectFilter.h"
#include "../log.h"
#include <cstring>

namespace Selection {

namespace {
    // List of marker editor IDs that should be filtered out
    // These are editor placeholders not meant for player interaction
    constexpr const char* kFilteredMarkers[] = {
        // Navigation and travel markers
        "TravelMarker",
        "DivineMarker",
        "TempleMarker",
        "MapMarker",
        "HorseMarker",
        "RoadMarker",

        // Cell and room markers
        "MultiBoundMarker",
        "PlaneMarker",
        "RoomMarker",
        "PortalMarker",
        "CollisionMarker",
        "COCMarkerHeading",

        // Generic position markers
        "XMarker",
        "XMarkerHeading",
        "XMarkerSnow",

        // Water markers
        "CellWaterCurrentMarker",
        "WaterCurrentMarker",
        "WaterCurrentZoneMarker",

        // Creature and NPC markers
        "DragonMarker",
        "DragonMarkerCrashStrip",
        "CritterLandingMarker_Small",
        "DoNotPlaceSmallCritterLandingMarkerHelper",

        // Scene markers
        "ComplexSceneMARKER",
        "MagicHatMarker",

        // DLC teleport markers
        "HarkonTeleportMarker",
        "CasExtMainTowerMarker01",
        "MiraakTeleportMarker",
        "DLC2MiraakTeleportMarker01",
        "DLC2MiraakTeleportMarker02",
        "DLC2MiraakTeleportMarker03",
        "DLC2MiraakTeleportMarker04",
        "DLC2MiraakTeleportMarker05",
    };

    constexpr size_t kFilteredMarkersCount = sizeof(kFilteredMarkers) / sizeof(kFilteredMarkers[0]);
}

bool ObjectFilter::IsFilteredMarker(const char* editorId)
{
    if (!editorId || editorId[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < kFilteredMarkersCount; ++i) {
        if (std::strcmp(editorId, kFilteredMarkers[i]) == 0) {
            return true;
        }
    }

    return false;
}

bool ObjectFilter::ShouldProcess(RE::TESObjectREFR* ref)
{
    // Basic null check - null references never pass
    if (!ref) {
        return false;
    }

    // Get the base object for type and editor ID checks
    RE::TESBoundObject* baseObj = ref->GetBaseObject();
    if (!baseObj) {
        return true;  // No base object, allow through (unusual case)
    }

    RE::FormID refFormId = ref->GetFormID();
    RE::FormID baseFormId = baseObj->GetFormID();

    // Filter: Static markers (editor placeholders)
    if (baseObj->GetFormType() == RE::FormType::Static) {
        const char* editorId = baseObj->GetFormEditorID();
        if (editorId && IsFilteredMarker(editorId)) {
            spdlog::trace("ObjectFilter: Filtered ref {:08X} (base {:08X}) - Static marker '{}'",
                refFormId, baseFormId, editorId);
            return false;
        }
    }

    return true;
}

} // namespace Selection
