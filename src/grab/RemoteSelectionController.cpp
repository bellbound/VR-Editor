#include "RemoteSelectionController.h"
#include "../FrameCallbackDispatcher.h"
#include "../selection/SelectionState.h"
#include "../selection/HoverStateManager.h"
#include "../util/VRNodes.h"
#include "../util/Raycast.h"
#include "../visuals/RaycastRenderer.h"
#include "../ui/MenuStateManager.h"
#include "../log.h"
#include <cmath>

namespace Grab {

namespace {
    // Collision layers that are selectable in edit mode
    // These are objects the user would typically want to move/edit
    bool IsSelectableLayer(RE::COL_LAYER layer) {
        switch (layer) {
            case RE::COL_LAYER::kStatic:            // Static world geometry (walls, buildings)
            case RE::COL_LAYER::kAnimStatic:        // Animated statics (moving platforms, gates)
            case RE::COL_LAYER::kTransparent:       // Thin geometry (banners, table cloths, cobwebs)
            case RE::COL_LAYER::kProps:             // Furniture, containers, larger props
            case RE::COL_LAYER::kTrees:             // Trees
            case RE::COL_LAYER::kClutter:           // Small items, clutter objects
            case RE::COL_LAYER::kDebrisSmall:       // Small debris
            case RE::COL_LAYER::kDebrisLarge:       // Large debris
            case RE::COL_LAYER::kTransparentSmallAnim: // Animated thin objects
            case RE::COL_LAYER::kClutterLarge:      // Large clutter objects
            case RE::COL_LAYER::kTerrain:           // Terrain objects
            case RE::COL_LAYER::kGround:            // Ground objects
                return true;
            // Explicitly NOT selectable:
            // - kWater, kTrigger, kTrap: Special purpose layers
            // Note: kActor/kCharController are handled separately via IsNPCSelectableLayer
            default:
                return false;
        }
    }

    // Check if this is an NPC-related collision layer
    bool IsNPCSelectableLayer(RE::COL_LAYER layer) {
        switch (layer) {
            case RE::COL_LAYER::kBiped:          // Biped actor body collision
            case RE::COL_LAYER::kCharController: // Character controller (movement capsule)
            case RE::COL_LAYER::kBipedNoCC:      // Biped without character controller
                return true;
            default:
                return false;
        }
    }
} // anonymous namespace

RemoteSelectionController* RemoteSelectionController::GetSingleton()
{
    static RemoteSelectionController instance;
    return &instance;
}

void RemoteSelectionController::Initialize()
{
    if (m_initialized) {
        spdlog::warn("RemoteSelectionController already initialized");
        return;
    }

    // Register for frame callbacks (only in edit mode)
    FrameCallbackDispatcher::GetSingleton()->Register(this, true);

    m_initialized = true;
    spdlog::info("RemoteSelectionController initialized");
}

void RemoteSelectionController::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Stop selection if active
    if (m_isActive) {
        StopSelection();
    }

    // Unregister from frame callbacks
    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    m_initialized = false;
    spdlog::info("RemoteSelectionController shutdown");
}

void RemoteSelectionController::StartSelection()
{
    if (m_isActive) {
        return;
    }

    m_isActive = true;
    m_hitPoint = RE::NiPoint3{0, 0, 0};

    // Ensure hover state is clear when starting
    Selection::HoverStateManager::GetSingleton()->Clear();

    spdlog::info("RemoteSelectionController: Started selection mode");
}

void RemoteSelectionController::StopSelection()
{
    if (!m_isActive) {
        return;
    }

    // Clear hover state (HoverStateManager handles highlight removal)
    Selection::HoverStateManager::GetSingleton()->Clear();

    // Hide ray
    RaycastRenderer::Hide();

    m_isActive = false;
    spdlog::info("RemoteSelectionController: Stopped selection mode");
}

bool RemoteSelectionController::OnTriggerReleased()
{
    // This method is no longer used - EditModeStateManager handles trigger release
    // and reads from HoverStateManager directly
    return Selection::HoverStateManager::GetSingleton()->HasHoveredObject();
}

bool RemoteSelectionController::HasHighlightedObject() const
{
    return Selection::HoverStateManager::GetSingleton()->HasHoveredObject();
}

RE::TESObjectREFR* RemoteSelectionController::GetHighlightedObject() const
{
    return Selection::HoverStateManager::GetSingleton()->GetHoveredObject();
}

void RemoteSelectionController::OnFrameUpdate(float deltaTime)
{
    if (!m_isActive) {
        return;
    }

    if (MenuStateManager::GetSingleton()->IsSelectionBlockingMenuOpen()) {
        RaycastRenderer::Hide();
        Selection::HoverStateManager::GetSingleton()->Clear();
        return;
    }

    // Cast all five rays - central ray for primary target, all rays for retention
    RE::NiPoint3 hitPoint;
    std::vector<RE::TESObjectREFR*> retentionHits;
    RE::TESObjectREFR* primaryHit = CastSelectionRays(hitPoint, retentionHits);

    // Update hover state via HoverStateManager (handles debounce and highlighting)
    // Pass both the primary hit and retention hits for sticky highlighting
    auto* hoverManager = Selection::HoverStateManager::GetSingleton();
    hoverManager->SetPendingHoverWithRetention(primaryHit, retentionHits);
    hoverManager->Update(deltaTime);

    if (primaryHit) {
        m_hitPoint = hitPoint;
    }

    // Update visuals
    UpdateVisuals();
}

RE::TESObjectREFR* RemoteSelectionController::CastSelectionRay(RE::NiPoint3& outHitPoint)
{
    // Delegate to single ray cast with normalized direction
    RE::NiPoint3 origin = GetHandPosition();
    RE::NiPoint3 direction = GetHandForward();
    return CastSingleRay(origin, direction, outHitPoint);
}

RE::TESObjectREFR* RemoteSelectionController::CastSelectionRays(RE::NiPoint3& outHitPoint, std::vector<RE::TESObjectREFR*>& retentionHits)
{
    retentionHits.clear();

    RE::NiPoint3 origin = GetHandPosition();
    RE::NiPoint3 forward = GetHandForward();
    RE::NiPoint3 right = GetHandRight();
    RE::NiPoint3 up = GetHandUp();

    // Cast central ray first (this is the primary selection target)
    RE::NiPoint3 centralHitPoint;
    RE::TESObjectREFR* primaryHit = CastSingleRay(origin, forward, centralHitPoint);
    if (primaryHit) {
        outHitPoint = centralHitPoint;
        retentionHits.push_back(primaryHit);
    }

    // Create 4 parallel rays with positional offsets (up, down, left, right)
    // All rays point in the same forward direction, just offset origins
    RE::NiPoint3 radialOrigins[4];

    // Up offset
    radialOrigins[0] = {
        origin.x + kRadialRayOffset * up.x,
        origin.y + kRadialRayOffset * up.y,
        origin.z + kRadialRayOffset * up.z
    };

    // Down offset
    radialOrigins[1] = {
        origin.x - kRadialRayOffset * up.x,
        origin.y - kRadialRayOffset * up.y,
        origin.z - kRadialRayOffset * up.z
    };

    // Right offset
    radialOrigins[2] = {
        origin.x + kRadialRayOffset * right.x,
        origin.y + kRadialRayOffset * right.y,
        origin.z + kRadialRayOffset * right.z
    };

    // Left offset
    radialOrigins[3] = {
        origin.x - kRadialRayOffset * right.x,
        origin.y - kRadialRayOffset * right.y,
        origin.z - kRadialRayOffset * right.z
    };

    // Cast radial rays (all parallel, same direction as central ray)
    for (int i = 0; i < 4; ++i) {
        RE::NiPoint3 radialHitPoint;
        RE::TESObjectREFR* radialHit = CastSingleRay(radialOrigins[i], forward, radialHitPoint);

        if (radialHit) {
            // Add to retention hits if not already present
            bool alreadyPresent = false;
            for (auto* existing : retentionHits) {
                if (existing == radialHit) {
                    alreadyPresent = true;
                    break;
                }
            }
            if (!alreadyPresent) {
                retentionHits.push_back(radialHit);
            }
        }
    }

    return primaryHit;
}

RE::TESObjectREFR* RemoteSelectionController::CastSingleRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, RE::NiPoint3& outHitPoint)
{
    // Normalize direction
    RE::NiPoint3 normDir = direction;
    float len = std::sqrt(normDir.x * normDir.x + normDir.y * normDir.y + normDir.z * normDir.z);
    if (len > 0.001f) {
        normDir.x /= len;
        normDir.y /= len;
        normDir.z /= len;
    }

    // Cast ray using Havok physics
    RaycastResult result = Raycast::CastRay(origin, normDir, kMaxRayDistance);

    if (!result.hit) {
        return nullptr;
    }

    // Check if the hit has an associated reference
    if (!result.hitRef) {
        return nullptr;
    }

    // Skip the player (always)
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (result.hitRef == player) {
        return nullptr;
    }

    // Check if we hit an NPC (actor that isn't the player)
    bool isNPC = result.hitRef->As<RE::Actor>() != nullptr;

    if (isNPC) {
        // For NPCs, accept if it's an NPC-selectable layer
        if (!IsNPCSelectableLayer(result.collisionLayer)) {
            return nullptr;
        }
    } else {
        // For non-NPCs, use the standard selectable layer check
        if (!IsSelectableLayer(result.collisionLayer)) {
            return nullptr;
        }
    }

    outHitPoint = result.hitPoint;
    return result.hitRef;
}

RE::NiPoint3 RemoteSelectionController::GetHandPosition() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        return hand->world.translate;
    }
    return RE::NiPoint3{0, 0, 0};
}

RE::NiPoint3 RemoteSelectionController::GetHandForward() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        // The controller's pointing direction is the Y axis of the rotation matrix
        // (Y+ axis points forward from the controller in Skyrim VR)
        const RE::NiMatrix3& rot = hand->world.rotate;
        return RE::NiPoint3{
            rot.entry[0][1],
            rot.entry[1][1],
            rot.entry[2][1]
        };
    }
    return RE::NiPoint3{0, 1, 0};
}

RE::NiPoint3 RemoteSelectionController::GetHandRight() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        // The X axis of the rotation matrix (right direction from controller)
        const RE::NiMatrix3& rot = hand->world.rotate;
        return RE::NiPoint3{
            rot.entry[0][0],
            rot.entry[1][0],
            rot.entry[2][0]
        };
    }
    return RE::NiPoint3{1, 0, 0};
}

RE::NiPoint3 RemoteSelectionController::GetHandUp() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        // The Z axis of the rotation matrix (up direction from controller)
        const RE::NiMatrix3& rot = hand->world.rotate;
        return RE::NiPoint3{
            rot.entry[0][2],
            rot.entry[1][2],
            rot.entry[2][2]
        };
    }
    return RE::NiPoint3{0, 0, 1};
}

void RemoteSelectionController::UpdateVisuals()
{
    constexpr float kLaserLength = 200.0f;

    RE::NiPoint3 origin = GetHandPosition();
    RE::NiPoint3 direction = GetHandForward();

    // Normalize
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len > 0.001f) {
        direction.x /= len;
        direction.y /= len;
        direction.z /= len;
    }

    RaycastRenderer::LineParams line;
    line.start = origin;
    line.end = {
        origin.x + direction.x * kLaserLength,
        origin.y + direction.y * kLaserLength,
        origin.z + direction.z * kLaserLength
    };

    if (RaycastRenderer::IsVisible()) {
        RaycastRenderer::Update(line, RaycastRenderer::BeamType::Default);
    } else {
        RaycastRenderer::Show(line, RaycastRenderer::BeamType::Default);
    }
}

} // namespace Grab
