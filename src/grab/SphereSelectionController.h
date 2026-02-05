#pragma once

#include "../IFrameUpdateListener.h"
#include "../interfaces/ThreeDUIInterface001.h"
#include "../util/InputManager.h"
#include <RE/Skyrim.h>

namespace Grab {

// SphereSelectionController: Handles volume-based selection of multiple objects
//
// Unlike RemoteSelectionController which selects a single object via ray casting,
// this controller allows selecting ALL objects within a sphere volume. The sphere
// is positioned at the ray hit point, and all objects inside are highlighted.
//
// Visual design:
// - Laser beam from hand to sphere center (via RaycastRenderer)
// - Sphere mesh at ray hit point (via 3DUI)
// - All objects inside sphere get hover highlight (via SphereHoverStateManager)
//
// This controller does NOT own input - EditModeStateManager tells it when to start/stop
class SphereSelectionController : public IFrameUpdateListener
{
public:
    static SphereSelectionController* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Called by EditModeStateManager when entering SphereSelecting state
    void StartSelection();

    // Called by EditModeStateManager when leaving SphereSelecting state
    void StopSelection();

    // Check if we currently have objects in the sphere
    bool HasObjectsInSphere() const;
    size_t GetObjectCount() const;

    // Configuration constants
    static constexpr float kDefaultRadius = 25.0f;        // Default sphere radius in game units
    static constexpr float kMinRadius = 2.5f;             // Minimum sphere radius
    static constexpr float kMaxRadius = 1500.0f;            // Maximum sphere radius
    static constexpr float kScanIntervalMs = 50.0f;       // Scan throttle in milliseconds (20 scans/sec)
    static constexpr float kMaxRayDistance = 10000.0f;     // Max distance for placement ray
    static constexpr float kThumbstickDeadzone = 0.3f;    // Deadzone for thumbstick input
    static constexpr float kRadiusScaleSpeed = 50.0f;     // Radius change per second at full thumbstick

private:
    SphereSelectionController() = default;
    ~SphereSelectionController() = default;
    SphereSelectionController(const SphereSelectionController&) = delete;
    SphereSelectionController& operator=(const SphereSelectionController&) = delete;

    // Cast ray to find sphere placement point
    bool CastPlacementRay(RE::NiPoint3& outHitPoint);

    // Scan for objects within the sphere
    void ScanObjectsInSphere(const RE::NiPoint3& center, float radius);

    // Check if a reference is selectable
    bool IsSelectable(RE::TESObjectREFR* ref) const;

    // Sphere visual management
    void CreateSphereVisual();
    void UpdateSphereVisual();
    void DestroySphereVisual();

    // Update the laser beam visual
    void UpdateLaserVisual();

    // Get hand position and direction
    RE::NiPoint3 GetHandPosition() const;
    RE::NiPoint3 GetHandForward() const;

    // Axis callback for thumbstick input (radius scaling)
    bool OnAxisInput(bool isLeft, uint32_t axisIndex, float x, float y);

    bool m_initialized = false;
    bool m_isActive = false;

    // Input callback ID
    InputManager::CallbackId m_axisCallbackId = InputManager::InvalidCallbackId;

    // Thumbstick state for per-frame radius updates
    float m_thumbstickY = 0.0f;

    // Sphere state
    RE::NiPoint3 m_sphereCenter{0, 0, 0};
    float m_radius = kDefaultRadius;
    float m_timeSinceLastScan = 0.0f;

    // 3DUI sphere visual
    P3DUI::Root* m_sphereRoot = nullptr;
    P3DUI::Element* m_sphereElement = nullptr;
};

} // namespace Grab
