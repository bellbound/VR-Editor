#pragma once

#include "../IFrameUpdateListener.h"
#include <RE/Skyrim.h>
#include <vector>

namespace Grab {

// RemoteSelectionController: Handles ray-based selection of distant objects
//
// When active (RemoteSelection state):
// - Casts ray from right hand each frame while trigger is held
// - Highlights objects under the ray
// - When trigger is released with something highlighted, signals GrabStateManager
//   to transition to RemotePlacement with that object
//
// This controller does NOT own any input - GrabStateManager tells it when to start/stop
class RemoteSelectionController : public IFrameUpdateListener
{
public:
    static RemoteSelectionController* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Called by GrabStateManager when entering RemoteSelection state
    void StartSelection();

    // Called by GrabStateManager when leaving RemoteSelection state
    void StopSelection();

    // Called by GrabStateManager when trigger is released during selection
    // Returns true if we had a highlighted object (and signals transition)
    bool OnTriggerReleased();

    // Check if we currently have a highlighted object (delegates to HoverStateManager)
    bool HasHighlightedObject() const;
    RE::TESObjectREFR* GetHighlightedObject() const;

    // Configuration
    static constexpr float kMaxRayDistance = 4000.0f;  // Max selection distance (doubled for long-range editing)
    static constexpr float kRayWidth = 2.0f;           // Visual ray width
    static constexpr float kRadialRayOffset = 3.75f;   // Positional offset in game units for parallel radial rays

private:
    RemoteSelectionController() = default;
    ~RemoteSelectionController() = default;
    RemoteSelectionController(const RemoteSelectionController&) = delete;
    RemoteSelectionController& operator=(const RemoteSelectionController&) = delete;

    // Cast central ray and find object under cursor
    RE::TESObjectREFR* CastSelectionRay(RE::NiPoint3& outHitPoint);

    // Cast all five rays and collect hits for debouncing
    // Returns the central ray hit (primary target), and fills retentionHits with all objects hit by any ray
    RE::TESObjectREFR* CastSelectionRays(RE::NiPoint3& outHitPoint, std::vector<RE::TESObjectREFR*>& retentionHits);

    // Cast a single ray in the given direction and return the hit object (or nullptr)
    RE::TESObjectREFR* CastSingleRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, RE::NiPoint3& outHitPoint);

    // Get hand position and direction for ray casting
    RE::NiPoint3 GetHandPosition() const;
    RE::NiPoint3 GetHandForward() const;
    RE::NiPoint3 GetHandRight() const;
    RE::NiPoint3 GetHandUp() const;

    // Update visual feedback (ray and highlight)
    void UpdateVisuals();

    bool m_initialized = false;
    bool m_isActive = false;

    // Hit point for ray endpoint (object tracking is in HoverStateManager)
    RE::NiPoint3 m_hitPoint;
};

} // namespace Grab
