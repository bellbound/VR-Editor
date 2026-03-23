#include "VirtualRaycastManager.h"
#include "../EditModeStateManager.h"
#include "../FrameCallbackDispatcher.h"
#include "../util/VRNodes.h"
#include "../log.h"
#include <cmath>
#include <limits>

namespace Selection {

VirtualRaycastManager* VirtualRaycastManager::GetSingleton()
{
    static VirtualRaycastManager instance;
    return &instance;
}

void VirtualRaycastManager::Initialize()
{
    if (m_initialized) {
        spdlog::warn("VirtualRaycastManager already initialized");
        return;
    }

    // Register for frame callbacks (only in edit mode)
    FrameCallbackDispatcher::GetSingleton()->Register(this, true);

    m_initialized = true;
    spdlog::info("VirtualRaycastManager initialized");
}

void VirtualRaycastManager::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    Clear();
    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    m_initialized = false;
    spdlog::info("VirtualRaycastManager shutdown");
}

void VirtualRaycastManager::OnFrameUpdate(float deltaTime)
{
    // Only active during ray-based selection mode
    auto* stateManager = EditModeStateManager::GetSingleton();
    if (!stateManager || stateManager->GetState() != EditModeState::Selecting) {
        // Clear results when not in selecting mode so stale data isn't used
        if (m_hoveredRef || !m_visibleRefs.empty()) {
            Clear();
        }
        return;
    }

    // Get ray from right hand controller (same source as RemoteSelectionController)
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (!hand) {
        return;
    }

    RE::NiPoint3 origin = hand->world.translate;
    const RE::NiMatrix3& rot = hand->world.rotate;
    // Y+ axis is the controller's pointing direction in Skyrim VR
    RE::NiPoint3 direction = {rot.entry[0][1], rot.entry[1][1], rot.entry[2][1]};

    // Normalize direction
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len > 0.001f) {
        direction.x /= len;
        direction.y /= len;
        direction.z /= len;
    }

    // Periodically scan for candidate refs (throttled)
    m_scanTimer += deltaTime;
    if (m_scanTimer >= kScanIntervalSeconds) {
        m_scanTimer = 0.0f;
        ScanCandidates();
    }

    // Test ray against all candidates each frame
    TestRayAgainstCandidates(origin, direction);

    // Tick down hysteresis timer (must happen after TestRayAgainstCandidates)
    if (m_pendingClearRef) {
        m_hysteresisTimer -= deltaTime;
        if (m_hysteresisTimer <= 0.0f) {
            // Hysteresis expired — clear hovered ref
            spdlog::trace("VirtualRaycastManager: Hysteresis expired for {:08X}", m_pendingClearRef->GetFormID());
            m_hoveredRef = nullptr;
            m_hoveredDistance = 0.0f;
            m_pendingClearRef = nullptr;
            m_hysteresisTimer = 0.0f;
        }
    }
}

void VirtualRaycastManager::Clear()
{
    m_visibleRefs.clear();
    m_hoveredRef = nullptr;
    m_hoveredDistance = 0.0f;
    m_pendingClearRef = nullptr;
    m_hysteresisTimer = 0.0f;
}

bool VirtualRaycastManager::IsVirtualRaycastCandidate(RE::TESObjectREFR* ref)
{
    if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
        return false;
    }

    auto* baseObj = ref->GetBaseObject();
    if (!baseObj) {
        return false;
    }

    switch (baseObj->GetFormType()) {
        case RE::FormType::Light:
            return true;
        // Future: case RE::FormType::Flora: return true;
        // Future: case RE::FormType::Tree: return true;
        default:
            return false;
    }
}

float VirtualRaycastManager::RaySphereIntersect(const RE::NiPoint3& O, const RE::NiPoint3& D,
                                                  const RE::NiPoint3& C, float R)
{
    // Vector from ray origin to sphere center
    RE::NiPoint3 OC = {O.x - C.x, O.y - C.y, O.z - C.z};

    // Quadratic formula coefficients (a = 1 since D is normalized)
    float b = 2.0f * (D.x * OC.x + D.y * OC.y + D.z * OC.z);
    float c = (OC.x * OC.x + OC.y * OC.y + OC.z * OC.z) - R * R;
    float discriminant = b * b - 4.0f * c;

    if (discriminant < 0.0f) {
        return -1.0f;  // No intersection
    }

    float sqrtDisc = std::sqrt(discriminant);
    float t0 = (-b - sqrtDisc) / 2.0f;  // Near intersection
    float t1 = (-b + sqrtDisc) / 2.0f;  // Far intersection

    if (t1 < 0.0f) {
        return -1.0f;  // Sphere is entirely behind the ray
    }

    // Return nearest positive t (if inside sphere, t0 is negative, use t1)
    return t0 >= 0.0f ? t0 : t1;
}

void VirtualRaycastManager::ScanCandidates()
{
    m_candidates.clear();

    auto* tes = RE::TES::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!tes || !player) {
        return;
    }

    tes->ForEachReferenceInRange(player, kMaxScanDistance, [&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
        if (!ref || ref == player) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        if (!IsVirtualRaycastCandidate(ref)) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        m_candidates.push_back({
            ref,
            ref->GetFormID(),
            ref->GetPosition()
        });

        return RE::BSContainer::ForEachResult::kContinue;
    });

    spdlog::trace("VirtualRaycastManager: Scanned {} candidates", m_candidates.size());
}

void VirtualRaycastManager::TestRayAgainstCandidates(const RE::NiPoint3& origin, const RE::NiPoint3& direction)
{
    m_visibleRefs.clear();

    RE::TESObjectREFR* frameHoveredRef = nullptr;
    float frameHoveredDistance = (std::numeric_limits<float>::max)();

    for (auto& candidate : m_candidates) {
        // Validate ref is still valid
        if (!candidate.ref || candidate.ref->IsDisabled() || candidate.ref->IsDeleted()) {
            continue;
        }

        // Update cached position (refs could theoretically move)
        candidate.position = candidate.ref->GetPosition();

        // Test visibility sphere (large) — ray passes through, does not stop
        float visT = RaySphereIntersect(origin, direction, candidate.position, kVisibilitySphereRadius);
        if (visT >= 0.0f) {
            m_visibleRefs.push_back(candidate.ref);
        }

        // Test selection sphere (small) — closest hit becomes hovered
        float selT = RaySphereIntersect(origin, direction, candidate.position, kSelectionSphereRadius);
        if (selT >= 0.0f && selT < frameHoveredDistance) {
            frameHoveredRef = candidate.ref;
            frameHoveredDistance = selT;
        }
    }

    // Apply hysteresis to the hovered ref
    UpdateHysteresis(0.0f, frameHoveredRef, frameHoveredDistance);
}

void VirtualRaycastManager::UpdateHysteresis(float /*deltaTime*/, RE::TESObjectREFR* frameHoveredRef, float frameHoveredDistance)
{
    if (frameHoveredRef) {
        // We have a hit this frame — update hovered ref, cancel any pending clear
        m_hoveredRef = frameHoveredRef;
        m_hoveredDistance = frameHoveredDistance;
        m_pendingClearRef = nullptr;
        m_hysteresisTimer = 0.0f;

    } else if (m_hoveredRef && !m_pendingClearRef) {
        // No hit this frame, but we had one and no hysteresis active yet — start countdown
        // The hovered ref stays reported during the countdown (prevents flicker)
        // Timer is decremented in OnFrameUpdate after this call
        m_pendingClearRef = m_hoveredRef;
        m_hysteresisTimer = kHysteresisTime;
    }
    // If m_pendingClearRef is already set, the timer countdown in OnFrameUpdate handles expiry
}

} // namespace Selection
