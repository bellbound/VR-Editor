#include "SphereSelectionController.h"
#include "../FrameCallbackDispatcher.h"
#include "../selection/SphereHoverStateManager.h"
#include "../selection/SelectionState.h"
#include "../selection/ObjectFilter.h"
#include "../util/VRNodes.h"
#include "../util/Raycast.h"
#include "../visuals/RaycastRenderer.h"
#include "../ui/MenuStateManager.h"
#include "../log.h"
#include <cmath>

namespace Grab {

namespace {
    // Collision layers that are selectable in sphere selection mode
    // Same as RemoteSelectionController for consistency
    bool IsSelectableLayer(RE::COL_LAYER layer) {
        switch (layer) {
            case RE::COL_LAYER::kStatic:
            case RE::COL_LAYER::kAnimStatic:
            case RE::COL_LAYER::kTransparent:
            case RE::COL_LAYER::kProps:
            case RE::COL_LAYER::kTrees:
            case RE::COL_LAYER::kClutter:
            case RE::COL_LAYER::kDebrisSmall:
            case RE::COL_LAYER::kDebrisLarge:
            case RE::COL_LAYER::kTransparentSmallAnim:
            case RE::COL_LAYER::kClutterLarge:
            case RE::COL_LAYER::kTerrain:
            case RE::COL_LAYER::kGround:
                return true;
            default:
                return false;
        }
    }

    // Helper to calculate distance between two points
    float Distance(const RE::NiPoint3& a, const RE::NiPoint3& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Helper to normalize a vector
    RE::NiPoint3 Normalize(const RE::NiPoint3& v) {
        float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len > 0.001f) {
            return RE::NiPoint3{v.x / len, v.y / len, v.z / len};
        }
        return RE::NiPoint3{0, 1, 0};
    }
} // anonymous namespace

SphereSelectionController* SphereSelectionController::GetSingleton()
{
    static SphereSelectionController instance;
    return &instance;
}

void SphereSelectionController::Initialize()
{
    if (m_initialized) {
        spdlog::warn("SphereSelectionController already initialized");
        return;
    }

    // Register for frame callbacks (only in edit mode)
    FrameCallbackDispatcher::GetSingleton()->Register(this, true);

    // Register axis callback for right thumbstick (axis 0)
    m_axisCallbackId = InputManager::GetSingleton()->AddVrAxisCallback(0,
        [this](bool isLeft, uint32_t axisIndex, float x, float y) {
            return this->OnAxisInput(isLeft, axisIndex, x, y);
        }
    );

    m_initialized = true;
    spdlog::info("SphereSelectionController initialized");
}

void SphereSelectionController::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Stop selection if active
    if (m_isActive) {
        StopSelection();
    }

    // Full cleanup of 3DUI resources on shutdown
    if (m_sphereRoot) {
        m_sphereRoot->SetVisible(false);
        m_sphereRoot = nullptr;
        m_sphereElement = nullptr;
    }

    // Unregister axis callback
    if (m_axisCallbackId != InputManager::InvalidCallbackId) {
        InputManager::GetSingleton()->RemoveVrAxisCallback(m_axisCallbackId);
        m_axisCallbackId = InputManager::InvalidCallbackId;
    }

    // Unregister from frame callbacks
    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    m_initialized = false;
    spdlog::info("SphereSelectionController shutdown");
}

bool SphereSelectionController::OnAxisInput(bool isLeft, uint32_t axisIndex, float x, float y)
{
    // Only care about right thumbstick
    if (isLeft) {
        return false;
    }

    // Early bail if not in sphere selection mode
    if (!m_isActive) {
        return false;
    }

    // Check if Y-axis is dominant and above deadzone (clear up/down input)
    float absX = std::abs(x);
    float absY = std::abs(y);

    if (absY > kThumbstickDeadzone && absY > absX) {
        // Y-axis is dominant - store for frame update and consume input
        m_thumbstickY = y;
        return true;
    }

    // No clear up/down input - clear stored value and don't consume
    // (allows left/right to pass through for other systems)
    m_thumbstickY = 0.0f;
    return false;
}

void SphereSelectionController::StartSelection()
{
    if (m_isActive) {
        return;
    }

    m_isActive = true;
    m_sphereCenter = RE::NiPoint3{0, 0, 0};
    m_radius = kDefaultRadius;
    m_timeSinceLastScan = kScanIntervalMs;  // Force immediate scan
    m_thumbstickY = 0.0f;  // Reset thumbstick state

    // Clear any previous sphere hover state
    Selection::SphereHoverStateManager::GetSingleton()->Clear();

    // Create the sphere visual
    CreateSphereVisual();

    spdlog::info("SphereSelectionController: Started sphere selection mode (radius: {})", m_radius);
}

void SphereSelectionController::StopSelection()
{
    if (!m_isActive) {
        return;
    }

    // Clear sphere hover state (removes all highlights)
    Selection::SphereHoverStateManager::GetSingleton()->Clear();

    // Hide ray
    RaycastRenderer::Hide();

    // Destroy sphere visual
    DestroySphereVisual();

    m_isActive = false;
    m_thumbstickY = 0.0f;  // Reset thumbstick state
    spdlog::info("SphereSelectionController: Stopped sphere selection mode");
}

bool SphereSelectionController::HasObjectsInSphere() const
{
    return Selection::SphereHoverStateManager::GetSingleton()->HasHoveredObjects();
}

size_t SphereSelectionController::GetObjectCount() const
{
    return Selection::SphereHoverStateManager::GetSingleton()->GetHoveredCount();
}

void SphereSelectionController::OnFrameUpdate(float deltaTime)
{
    if (!m_isActive) {
        return;
    }

    if (MenuStateManager::GetSingleton()->IsSelectionBlockingMenuOpen()) {
        RaycastRenderer::Hide();
        Selection::SphereHoverStateManager::GetSingleton()->Clear();
        return;
    }

    // Apply radius change from thumbstick Y input
    if (std::abs(m_thumbstickY) > 0.01f) {
        // Remap [deadzone, 1.0] to [0.0, 1.0] for smooth scaling
        float sign = (m_thumbstickY > 0.0f) ? 1.0f : -1.0f;
        float absY = std::abs(m_thumbstickY);
        float remapped = (absY - kThumbstickDeadzone) / (1.0f - kThumbstickDeadzone);
        if (remapped < 0.0f) remapped = 0.0f;  // Clamp to positive

        float radiusChange = sign * remapped * kRadiusScaleSpeed * deltaTime;
        m_radius += radiusChange;

        // Clamp to valid range
        if (m_radius < kMinRadius) {
            m_radius = kMinRadius;
        } else if (m_radius > kMaxRadius) {
            m_radius = kMaxRadius;
        }
    }

    // Cast ray to find sphere placement point
    RE::NiPoint3 hitPoint;
    bool hasHit = CastPlacementRay(hitPoint);

    if (hasHit) {
        m_sphereCenter = hitPoint;
    }

    // Update sphere visual position
    UpdateSphereVisual();

    // Update laser beam
    UpdateLaserVisual();

    // Throttled object scanning
    m_timeSinceLastScan += deltaTime * 1000.0f;  // Convert to ms
    if (m_timeSinceLastScan >= kScanIntervalMs) {
        m_timeSinceLastScan = 0.0f;
        ScanObjectsInSphere(m_sphereCenter, m_radius);
    }
}

bool SphereSelectionController::CastPlacementRay(RE::NiPoint3& outHitPoint)
{
    RE::NiPoint3 origin = GetHandPosition();
    RE::NiPoint3 direction = Normalize(GetHandForward());

    RaycastResult result = Raycast::CastRay(origin, direction, kMaxRayDistance);

    if (result.hit) {
        outHitPoint = result.hitPoint;
        return true;
    }

    // No hit - place sphere at max distance
    outHitPoint = RE::NiPoint3{
        origin.x + direction.x * kMaxRayDistance,
        origin.y + direction.y * kMaxRayDistance,
        origin.z + direction.z * kMaxRayDistance
    };
    return false;
}

void SphereSelectionController::ScanObjectsInSphere(const RE::NiPoint3& center, float radius)
{
    std::vector<RE::TESObjectREFR*> found;
    auto* tes = RE::TES::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!tes || !player) {
        Selection::SphereHoverStateManager::GetSingleton()->SetHoveredObjects(found);
        return;
    }

    // Use ForEachReferenceInRange with player as origin and expanded radius
    // We use 2x radius to ensure we catch objects when sphere center is away from player
    // Then do fine-grained distance check from actual sphere center
    float searchRadius = radius * 2.0f + Distance(center, player->GetPosition());

    tes->ForEachReferenceInRange(player, searchRadius, [&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
        if (!ref) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Skip player
        if (ref == player) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Check if selectable
        if (!IsSelectable(ref)) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Check if object passes the selection filter
        if (!Selection::ObjectFilter::ShouldProcess(ref)) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Fine-grained distance check from sphere center
        float dist = Distance(ref->GetPosition(), center);
        if (dist <= radius) {
            found.push_back(ref);
        }

        return RE::BSContainer::ForEachResult::kContinue;
    });

    // Update hover state manager with found objects
    Selection::SphereHoverStateManager::GetSingleton()->SetHoveredObjects(found);

    if (!found.empty()) {
        spdlog::trace("SphereSelectionController: Found {} objects in sphere", found.size());
    }
}

bool SphereSelectionController::IsSelectable(RE::TESObjectREFR* ref) const
{
    if (!ref) {
        return false;
    }

    // Skip disabled references
    if (ref->IsDisabled()) {
        return false;
    }

    // Skip deleted references
    if (ref->IsDeleted()) {
        return false;
    }

    // Check if the reference has 3D loaded (visible in scene)
    if (!ref->Get3D()) {
        return false;
    }

    // Get the base object
    auto* baseObj = ref->GetBaseObject();
    if (!baseObj) {
        return false;
    }

    // Check object type - we want movable objects, not actors
    // Skip actors (NPCs, creatures) for now - sphere selection is for props/clutter
    if (ref->As<RE::Actor>()) {
        return false;
    }

    // Accept various static/movable object types
    switch (baseObj->GetFormType()) {
        case RE::FormType::Static:
        case RE::FormType::MovableStatic:
        case RE::FormType::Container:
        case RE::FormType::Door:
        case RE::FormType::Light:
        case RE::FormType::Furniture:
        case RE::FormType::Activator:
        case RE::FormType::Tree:
        case RE::FormType::Flora:
        case RE::FormType::Misc:
        case RE::FormType::Weapon:
        case RE::FormType::Grass:
        case RE::FormType::Armor:
        case RE::FormType::Book:
        case RE::FormType::IdleMarker:
        case RE::FormType::Ingredient:
        case RE::FormType::AnimatedObject:
        case RE::FormType::AlchemyItem:
        case RE::FormType::Ammo:
        case RE::FormType::Scroll:
        case RE::FormType::SoulGem:
        case RE::FormType::ArtObject:
        case RE::FormType::VolumetricLighting:
            return true;
        default:
            return false;
    }
}

void SphereSelectionController::CreateSphereVisual()
{
    // If we already have a sphere root, just show it
    if (m_sphereRoot) {
        m_sphereRoot->SetVisible(true);
        spdlog::trace("SphereSelectionController: Reusing existing sphere visual");
        return;
    }

    auto* api = P3DUI::GetInterface001();
    if (!api) {
        spdlog::warn("SphereSelectionController: 3DUI interface not available");
        return;
    }

    // Get or create root for sphere - non-interactive, world-positioned
    P3DUI::RootConfig rootCfg = P3DUI::RootConfig::Default("sphere_select", "VRBuildMode");
    rootCfg.interactive = false;
    m_sphereRoot = api->GetOrCreateRoot(rootCfg);

    if (!m_sphereRoot) {
        spdlog::error("SphereSelectionController: Failed to create sphere root");
        return;
    }

    // Configure for world-space positioning (no VR anchor, no facing)
    m_sphereRoot->SetVRAnchor(P3DUI::VRAnchorType::None);
    m_sphereRoot->SetFacingMode(P3DUI::FacingMode::None);

    // Create sphere element
    P3DUI::ElementConfig elemCfg = P3DUI::ElementConfig::Default("sphere");
    elemCfg.modelPath = "meshes\\3DUI\\unit-sphere.nif";
    // Unit sphere mesh (radius 1.0), scale directly to match selection radius
    elemCfg.scale = m_radius;
    elemCfg.facingMode = P3DUI::FacingMode::None;
    elemCfg.smoothingFactor = 17.0f;

    m_sphereElement = api->CreateElement(elemCfg);
    if (m_sphereElement) {
        m_sphereRoot->AddChild(m_sphereElement);
        m_sphereRoot->SetVisible(true);
        spdlog::info("SphereSelectionController: Created sphere visual");
    } else {
        spdlog::error("SphereSelectionController: Failed to create sphere element");
    }
}

void SphereSelectionController::UpdateSphereVisual()
{
    if (!m_sphereRoot) {
        return;
    }

    // Ensure visibility - cell changes can hide the root
    m_sphereRoot->SetVisible(true);

    // Update sphere position to match ray hit point
    m_sphereRoot->SetLocalPosition(m_sphereCenter.x, m_sphereCenter.y, m_sphereCenter.z);

    // Update scale if radius changed (unit sphere, scale = radius)
    if (m_sphereElement) {
        m_sphereElement->SetScale(m_radius);
    }
}

void SphereSelectionController::DestroySphereVisual()
{
    // Just hide the sphere - don't null pointers so we can reuse on re-entry
    // GetOrCreateRoot returns the existing root when called again
    if (m_sphereRoot) {
        m_sphereRoot->SetVisible(false);
        spdlog::trace("SphereSelectionController: Hidden sphere visual");
    }
}

void SphereSelectionController::UpdateLaserVisual()
{
    RE::NiPoint3 origin = GetHandPosition();

    RaycastRenderer::LineParams line;
    line.start = origin;
    line.end = m_sphereCenter;

    if (RaycastRenderer::IsVisible()) {
        RaycastRenderer::Update(line, RaycastRenderer::BeamType::Default);
    } else {
        RaycastRenderer::Show(line, RaycastRenderer::BeamType::Default);
    }
}

RE::NiPoint3 SphereSelectionController::GetHandPosition() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        return hand->world.translate;
    }
    return RE::NiPoint3{0, 0, 0};
}

RE::NiPoint3 SphereSelectionController::GetHandForward() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        // The controller's pointing direction is the Y axis of the rotation matrix
        const RE::NiMatrix3& rot = hand->world.rotate;
        return RE::NiPoint3{
            rot.entry[0][1],
            rot.entry[1][1],
            rot.entry[2][1]
        };
    }
    return RE::NiPoint3{0, 1, 0};
}

} // namespace Grab
