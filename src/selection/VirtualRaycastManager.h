#pragma once

#include "../IFrameUpdateListener.h"
#include <RE/Skyrim.h>
#include <vector>

namespace Selection {

// VirtualRaycastManager: Proximity-based discovery + virtual raycasting for objects
// without collision geometry
//
// Objects like light sources have no Havok collision, so the physics-based ray in
// RemoteSelectionController can never hit them. This manager provides:
//
// 1. Proximity-based visibility: All eligible objects within an adaptive radius around
//    the player get icons via ObjectHandleVisualizer. The radius starts at 1024 units
//    and halves (512, 256) if the number of candidates exceeds the icon slot limit (32).
//    If still over the limit at minimum radius, the closest 32 are shown.
//
// 2. Virtual raycast for hover: The controller ray is tested against a small selection
//    sphere at each candidate's position. The closest hit becomes the hovered ref.
//
// The manager runs a periodic scan (ForEachReferenceInRange) to discover eligible
// objects and compute the visible set, then performs per-frame ray-sphere tests for
// hover detection against the cached candidate list.
//
// RemoteSelectionController queries GetHoveredRef() and compares its distance against
// the physics raycast hit — whichever is closer wins.
//
// Usage:
// - Initialize() during kDataLoaded (registers for edit-mode-only frame callbacks)
// - RemoteSelectionController queries GetHoveredRef()/GetHoveredDistance() each frame
// - ObjectHandleVisualizer queries GetVisibleRefs() at 15 FPS for icon rendering
// - Clear() when exiting selection mode
//
class VirtualRaycastManager : public IFrameUpdateListener
{
public:
    static VirtualRaycastManager* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Query: refs within adaptive proximity radius (updated at scan rate, not per-frame)
    const std::vector<RE::TESObjectREFR*>& GetVisibleRefs() const { return m_visibleRefs; }

    // Query: closest ref whose selection sphere the ray intersects (or nullptr)
    RE::TESObjectREFR* GetHoveredRef() const { return m_hoveredRef; }

    // Query: distance along ray to hovered ref's selection sphere intersection
    float GetHoveredDistance() const { return m_hoveredDistance; }

    // Clear all state (called when exiting selection mode)
    void Clear();

    // Check if a reference is eligible for virtual raycasting (lights, future: foliage)
    // Public so ObjectHandleVisualizer can filter selected refs to only show handles for eligible types
    static bool IsVirtualRaycastCandidate(RE::TESObjectREFR* ref);

    // Light selection toggle - persisted to cosave per save game
    static void SetLightSelectionEnabled(bool enabled) { s_lightSelectionEnabled = enabled; }
    static bool IsLightSelectionEnabled() { return s_lightSelectionEnabled; }

    // Configuration
    static constexpr float kSelectionSphereRadius = 25.0f;    // Sphere radius to acquire hover
    static constexpr float kUnhoverSphereRadius = 40.0f;      // Larger sphere radius to retain hover (hysteresis)
    static constexpr float kMaxScanDistance = 2000.0f;         // Max distance from player to scan refs
    static constexpr float kBaseVisibilityRadius = 1024.0f;    // Default radius for showing light icons
    static constexpr int kMaxVisibleRefs = 32;                 // Max visible refs (matches icon pool size)
    static constexpr float kScanIntervalSeconds = 0.1f;        // Throttle for ref discovery scan (100ms)

private:
    VirtualRaycastManager() = default;
    ~VirtualRaycastManager() = default;
    VirtualRaycastManager(const VirtualRaycastManager&) = delete;
    VirtualRaycastManager& operator=(const VirtualRaycastManager&) = delete;

    // Cached candidate from periodic scan
    struct CandidateRef {
        RE::TESObjectREFR* ref;
        RE::FormID formId;
        RE::NiPoint3 position;
        float distanceSq;  // squared distance from player, computed during scan
    };

    // Ray-sphere intersection test
    // Returns t (distance along ray to nearest intersection) or -1.0f if no hit
    static float RaySphereIntersect(const RE::NiPoint3& origin, const RE::NiPoint3& direction,
                                     const RE::NiPoint3& center, float radius);

    // Scan for candidate refs near the player (throttled)
    void ScanCandidates();

    // Test ray against all candidates for hover detection (selection sphere only)
    void TestRayAgainstCandidates(const RE::NiPoint3& origin, const RE::NiPoint3& direction);

    bool m_initialized = false;

    // Candidate discovery (throttled scan)
    std::vector<CandidateRef> m_candidates;
    float m_scanTimer = 0.0f;

    // Proximity-based visible refs (updated at scan rate)
    std::vector<RE::TESObjectREFR*> m_visibleRefs;

    // Hovered ref (sphere-size hysteresis: kSelectionSphereRadius to acquire, kUnhoverSphereRadius to retain)
    RE::TESObjectREFR* m_hoveredRef = nullptr;
    float m_hoveredDistance = 0.0f;

    // Toggle state (persisted to cosave)
    static inline bool s_lightSelectionEnabled = false;
};

} // namespace Selection
