#pragma once

#include "../IFrameUpdateListener.h"
#include "../util/InputManager.h"
#include "../util/PositioningUtil.h"
#include "../util/Raycast.h"
#include "TransformSmoother.h"
#include "ThumbstickAccelerationHelper.h"
#include "RemoteNPCPlacementManager.h"
#include "HandRotationTransformer.h"
#include "SnapToGridController.h"
#include "../util/UUID.h"
#include <RE/Skyrim.h>
#include <vector>

namespace Grab {

// Information about a selected object being remotely manipulated
struct RemoteGrabObject {
    RE::TESObjectREFR* ref = nullptr;
    RE::FormID formId = 0;
    RE::NiTransform initialTransform;  // Transform when entering remote mode (for undo)
    RE::NiPoint3 offsetFromCenter;     // Offset from the group center point

    // Original Euler angles read directly from game data (NOT converted from matrix)
    // Used at finalization to avoid lossy Matrix→Euler conversion.
    // Final angles = initialEulerAngles + Z delta (lossless for Z-only rotation)
    RE::NiPoint3 initialEulerAngles;

    // Collision management during grab
    PositioningUtil::CollisionState collisionState;  // Tracks disabled collision state
    bool collisionDisabled = false;                   // True if collision was disabled on enter

    // Ground snap state - tracks whether object was snapped during movement
    // Used during finalization to preserve the snapped position/rotation
    bool wasGroundSnapped = false;
};

// RemoteGrabController: Manages "laser pointer" style remote object manipulation
//
// DESIGN:
// - Completely decoupled from GrabStateManager/HandPlacement
// - Works with SelectionState (single or multi-selection)
// - Entered via grip button in Selecting mode, exited on grip release
//
// MULTI-SELECTION:
// - Center point is calculated as: z = min(all z), x/y = average(all x/y)
// - Each object maintains its offset from center
// - On enter, objects snap to the laser pointer position
// - All objects move together maintaining relative positions
//
// Controls (right thumbstick while active):
//   - Forward/Backward: Move group along ray (away/toward player)
//   - Left/Right: Rotate group around Z axis
//
// Visual feedback: Line rendered between hand and center point via RaycastRenderer
//
class RemoteGrabController : public IFrameUpdateListener
{
public:
    static RemoteGrabController* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // Check if remote grab mode is currently active
    bool IsActive() const { return m_isActive; }

    // Called by EditModeStateManager when entering remote placement mode
    // Reads from SelectionState, calculates center, snaps objects to laser position
    void OnEnter();

    // Called by EditModeStateManager when exiting remote placement mode
    // Records action(s) to history, finalizes positions
    void OnExit();

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Configuration
    static constexpr float kMinDistance = 50.0f;           // Minimum distance from hand
    static constexpr float kMoveSpeed = 200.0f;            // Units per second for distance change (normal)
    static constexpr float kFastMoveMultiplier = 3.0f;     // Multiplier for fast mode
    static constexpr float kFastModeThreshold = 1.0f;      // Seconds before fast mode activates
    static constexpr float kFarDistanceThreshold = 800.0f; // Distance beyond which fast mode is always active
    static constexpr float kSlowdownDistance = 130.0f;     // Distance at which fast mode disables when approaching
    static constexpr float kSpeedTransitionTime = 0.15f;   // Seconds to interpolate between speeds
    static constexpr float kRotateSpeed = 2.0f;            // Radians per second for Z rotation
    static constexpr float kThumbstickDeadzone = 0.15f;    // Ignore small movements
    static constexpr float kGroundRayMaxDistance = 2000.0f; // Max distance for ground snap raycast
    static constexpr float kScaleSpeed = 1.0f;             // Scale multiplier change per second
    static constexpr float kMinScale = 0.1f;               // Minimum allowed scale
    static constexpr float kMaxScale = 100.0f;             // Maximum allowed scale

private:
    RemoteGrabController() = default;
    ~RemoteGrabController() = default;
    RemoteGrabController(const RemoteGrabController&) = delete;
    RemoteGrabController& operator=(const RemoteGrabController&) = delete;

    // Axis callback for thumbstick input
    bool OnAxisInput(bool isLeft, uint32_t axisIndex, float x, float y);

    // B-button callback for ground snap modifier
    bool OnBButtonPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    // Left trigger callback for hand rotation mode
    bool OnLeftTrigger(bool isLeft, uint32_t axisIndex, float x, float y);

    // Left A button callback for scale mode
    bool OnLeftAButton(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    // Get hand position and forward direction (right hand)
    RE::NiPoint3 GetHandPosition() const;
    RE::NiPoint3 GetHandForward() const;

    // Calculate the center point for a group of objects
    // z = minimum z of all objects, x/y = average of all objects
    RE::NiPoint3 CalculateSelectionCenter(const std::vector<RemoteGrabObject>& objects) const;

    // Calculate target center position along the ray
    RE::NiPoint3 CalculateTargetPosition() const;

    // Apply transform to a single object (position + optional rotation)
    void ApplyTransformToObject(RE::TESObjectREFR* ref, const RE::NiTransform& transform);

    // Update all objects based on current center position and rotation
    // deltaTime is used for snap-to-grid motion smoothing
    void UpdateAllObjects(const RE::NiPoint3& centerPos, const RE::NiMatrix3& rotation, float deltaTime);

    // Update ray visualization
    void UpdateRayVisualization();

    // Split frame update into mode-specific methods
    void UpdateNPCMode(float deltaTime);
    void UpdateStandardMode(float deltaTime);

    // Process thumbstick input with deadzone and axis isolation
    // Returns: processed X and Y values (only dominant axis is non-zero)
    void ProcessThumbstickInput(float& outX, float& outY) const;

    // Record all object transforms to action history
    void RecordActions();

    // Left-hand gesture mode helpers (rotation + translation)
    void StartLeftHandRotation();    // Called when left trigger pressed
    void UpdateLeftHandRotation();   // Called each frame while trigger held (now a no-op, transformers updated in UpdateStandardMode)
    void FinishLeftHandRotation();   // Called when left trigger released
    void RemoveLeftHandUndoEntries(); // Called on exit to clean up temp undo actions

    // Finalize object positions (restore collision, sync Havok)
    void FinalizePositions();

    // Disable collisions on all grabbed objects at the start of remote grab
    // Exception: If the player is standing on an object, its collision is kept enabled
    void DisableCollisionsOnEnter();

    // Restore collisions on all grabbed objects when exiting remote grab
    void RestoreCollisionsOnExit();

    bool m_initialized = false;
    bool m_isActive = false;
    bool m_isNPCMode = false;  // True when manipulating an NPC (delegates to RemoteNPCPlacementManager)

    // All objects being remotely manipulated
    std::vector<RemoteGrabObject> m_objects;

    // The calculated center point (z = min, x/y = average)
    RE::NiPoint3 m_centerPoint;

    // Current state
    float m_distance = 100.0f;              // Current distance from hand along ray
    float m_zRotation = 0.0f;               // Accumulated Z-axis rotation
    RE::NiMatrix3 m_baseRotation;           // Initial rotation (identity for groups)

    // Thumbstick state (updated by axis callback)
    float m_thumbstickX = 0.0f;             // Left/right for rotation
    float m_thumbstickY = 0.0f;             // Forward/back for distance

    // Time-based acceleration helper (replaces manual acceleration state)
    ThumbstickAccelerationHelper m_accelerationHelper;

    // Transform smoother for center position interpolation
    TransformSmoother m_smoother;

    // Snap-to-grid controller - handles position snapping with motion smoothing
    SnapToGridController m_snapController;

    // Input callback IDs
    InputManager::CallbackId m_axisCallbackId = InputManager::InvalidCallbackId;
    InputManager::CallbackId m_bButtonCallbackId = InputManager::InvalidCallbackId;
    InputManager::CallbackId m_leftTriggerCallbackId = InputManager::InvalidCallbackId;
    InputManager::CallbackId m_leftAButtonCallbackId = InputManager::InvalidCallbackId;

    // Left-hand transform state
    bool m_leftTriggerHeld = false;           // True while left trigger is pressed
    float m_leftTriggerValue = 0.0f;          // Current trigger axis value

    // Hand rotation transformer - handles smoothed, inverted rotation from left hand
    HandRotationTransformer m_rotationTransformer;

    // Per-object state captured when a left-hand gesture starts
    // Used for the undo entry's "initial" state
    struct LeftHandGestureStartState {
        RE::NiTransform transform;
        RE::NiPoint3 eulerAngles;
    };
    std::vector<LeftHandGestureStartState> m_leftHandStartStates;

    // Tracks rotation/translation undo entries for cleanup on exit
    // These are removed when exiting RemoteGrabState since the final placement action includes them
    std::vector<Util::ActionId> m_leftHandUndoEntries;

    // Ground snap toggle state (press B to toggle on/off)
    // Resets to OFF when starting a new grab
    bool m_groundSnapEnabled = false;

    // Scale mode state (hold left A/X button to enable)
    // While held, right thumbstick Y axis scales objects instead of moving
    bool m_scaleModeActive = false;
    float m_accumulatedScaleMultiplier = 1.0f;  // Total scale change during this grab

    // Group move mode - when enabled, touching objects are auto-included in selection
    // Persists across grabs, toggled via UI button
    static inline bool s_groupMoveEnabled = false;

public:
    // Toggle group move mode on/off
    static void SetGroupMoveEnabled(bool enabled) { s_groupMoveEnabled = enabled; }
    static bool IsGroupMoveEnabled() { return s_groupMoveEnabled; }

    // Toggle snap to grid mode on/off
    static void SetSnapToGridEnabled(bool enabled) { GetSingleton()->m_snapController.SetEnabled(enabled); }
    static bool IsSnapToGridEnabled() { return GetSingleton()->m_snapController.IsEnabled(); }

    // Access to snap controller for configuration (grid sizes, rotation degrees)
    SnapToGridController& GetSnapController() { return m_snapController; }

    // Check if an object is a clutter/physics-enabled moveable object
    // (plates, cups, books, etc. - not static architecture)
    static bool IsClutterOrPhysicsObject(RE::TESObjectREFR* ref);
};

} // namespace Grab
