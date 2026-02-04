#include "RemoteGrabController.h"
#include "DeferredCollisionUpdateManager.h"
#include "GroupMoveResolver.h"
#include "RemoteGrabTransformCalculator.h"
#include "../FrameCallbackDispatcher.h"
#include "../actions/ActionHistoryRepository.h"
#include "../selection/SelectionState.h"
#include "../selection/DelayedHighlightRefreshManager.h"
#include "../util/VRNodes.h"
#include "../util/PositioningUtil.h"
#include "../util/RotationMath.h"
#include "../visuals/RaycastRenderer.h"
#include "../visuals/ObjectHighlighter.h"
#include "../util/ActionLogger.h"
#include "../log.h"
#include <cmath>
#include <limits>

namespace Grab {

// Bring rotation math utilities into scope
using Util::RotationMath::ExtractZRotation;
using Util::RotationMath::EulerToMatrix;
using Util::RotationMath::RotatePointAroundZ;

RemoteGrabController* RemoteGrabController::GetSingleton()
{
    static RemoteGrabController instance;
    return &instance;
}

void RemoteGrabController::Initialize()
{
    if (m_initialized) {
        spdlog::warn("RemoteGrabController already initialized");
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

    // Register B-button callback for ground snap modifier
    // B button is k_EButton_ApplicationMenu on most VR controllers
    m_bButtonCallbackId = InputManager::GetSingleton()->AddVrButtonCallback(
        vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu),
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnBButtonPressed(isLeft, isReleased, buttonId);
        }
    );

    // Register left trigger axis callback for hand rotation mode
    // Axis 1 is the trigger on most VR controllers
    m_leftTriggerCallbackId = InputManager::GetSingleton()->AddVrAxisCallback(1,
        [this](bool isLeft, uint32_t axisIndex, float x, float y) {
            return this->OnLeftTrigger(isLeft, axisIndex, x, y);
        }
    );

    // Register left A button (X button on Oculus) callback for scale mode
    // k_EButton_A is the A/X button
    m_leftAButtonCallbackId = InputManager::GetSingleton()->AddVrButtonCallback(
        vr::ButtonMaskFromId(vr::k_EButton_A),
        [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
            return this->OnLeftAButton(isLeft, isReleased, buttonId);
        }
    );

    m_initialized = true;
    spdlog::info("RemoteGrabController initialized");
}

void RemoteGrabController::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Exit remote mode if active
    if (m_isActive) {
        OnExit();
    }

    // Unregister from frame callbacks
    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    // Unregister axis callback
    if (m_axisCallbackId != InputManager::InvalidCallbackId) {
        InputManager::GetSingleton()->RemoveVrAxisCallback(m_axisCallbackId);
        m_axisCallbackId = InputManager::InvalidCallbackId;
    }

    // Unregister B-button callback
    if (m_bButtonCallbackId != InputManager::InvalidCallbackId) {
        InputManager::GetSingleton()->RemoveVrButtonCallback(m_bButtonCallbackId);
        m_bButtonCallbackId = InputManager::InvalidCallbackId;
    }

    // Unregister left trigger callback
    if (m_leftTriggerCallbackId != InputManager::InvalidCallbackId) {
        InputManager::GetSingleton()->RemoveVrAxisCallback(m_leftTriggerCallbackId);
        m_leftTriggerCallbackId = InputManager::InvalidCallbackId;
    }

    // Unregister left A button callback
    if (m_leftAButtonCallbackId != InputManager::InvalidCallbackId) {
        InputManager::GetSingleton()->RemoveVrButtonCallback(m_leftAButtonCallbackId);
        m_leftAButtonCallbackId = InputManager::InvalidCallbackId;
    }

    m_initialized = false;
    spdlog::info("RemoteGrabController shutdown");
}

bool RemoteGrabController::OnAxisInput(bool isLeft, uint32_t axisIndex, float x, float y)
{
    // Only care about right thumbstick
    if (isLeft) {
        return false;
    }

    // Early bail if not in remote grab mode
    if (!m_isActive) {
        return false;
    }

    // Store thumbstick values for use in OnFrameUpdate
    m_thumbstickX = x;
    m_thumbstickY = y;

    // Consume input when remote mode is active
    return true;
}

bool RemoteGrabController::OnBButtonPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only care about right controller B button
    if (isLeft) {
        return false;
    }

    // Only consume input when remote mode is active
    if (!m_isActive) {
        return false;
    }

    // NPC mode doesn't support ground snapping
    if (m_isNPCMode) {
        return false;
    }

    // Toggle mode: only react to press, not release
    if (isReleased) {
        // Ignore release - toggle happens on press
        return true;
    }

    // Toggle ground snap on/off
    m_groundSnapEnabled = !m_groundSnapEnabled;
    spdlog::info("RemoteGrabController: B-button toggled ground snap {}", m_groundSnapEnabled ? "ON" : "OFF");

    // Consume input when remote mode is active
    return true;
}

bool RemoteGrabController::OnLeftTrigger(bool isLeft, uint32_t axisIndex, float x, float y)
{
    // Only care about left controller trigger
    if (!isLeft) {
        return false;
    }

    // Only active when remote mode is active and not in NPC mode
    if (!m_isActive || m_isNPCMode) {
        return false;
    }

    // Trigger axis value is in x (0.0 to 1.0)
    m_leftTriggerValue = x;

    // Threshold for trigger activation (slight press = rotation mode)
    constexpr float kTriggerPressThreshold = 0.5f;
    constexpr float kTriggerReleaseThreshold = 0.3f;  // Hysteresis to avoid flickering

    bool wasTriggerHeld = m_leftTriggerHeld;

    if (!m_leftTriggerHeld && x >= kTriggerPressThreshold) {
        // Trigger pressed - start rotation mode
        // Precise rotation only supported for single selection
        if (m_objects.size() > 1) {
            RE::DebugNotification("Precise Rotation is only Supported for single selection");
            return false;  // Don't enter rotation mode
        }
        m_leftTriggerHeld = true;
        StartLeftHandRotation();
    } else if (m_leftTriggerHeld && x < kTriggerReleaseThreshold) {
        // Trigger released - finish rotation mode
        m_leftTriggerHeld = false;
        FinishLeftHandRotation();
    }

    // Consume input when rotation mode is active
    return m_leftTriggerHeld || wasTriggerHeld;
}

bool RemoteGrabController::OnLeftAButton(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only care about left controller A button (X button on Oculus)
    if (!isLeft) {
        return false;
    }

    // Only active when remote mode is active and not in NPC mode
    if (!m_isActive || m_isNPCMode) {
        return false;
    }

    // Update scale mode state based on button press/release
    if (!isReleased) {
        // Button pressed - enable scale mode
        if (!m_scaleModeActive) {
            m_scaleModeActive = true;
            spdlog::info("RemoteGrabController: Scale mode enabled");
        }
    } else {
        // Button released - disable scale mode
        if (m_scaleModeActive) {
            m_scaleModeActive = false;
            spdlog::info("RemoteGrabController: Scale mode disabled");
        }
    }

    // Consume input when remote mode is active
    return true;
}

void RemoteGrabController::OnEnter()
{
    if (m_isActive) {
        spdlog::warn("RemoteGrabController::OnEnter called but already active");
        return;
    }

    // Get selection from SelectionState
    auto* selectionState = Selection::SelectionState::GetSingleton();
    const auto& selections = selectionState->GetSelection();

    if (selections.empty()) {
        spdlog::warn("RemoteGrabController::OnEnter: Nothing selected");
        return;
    }

    // AUTO-INCLUDE TOUCHING OBJECTS (Group Move):
    // Before processing the grab, find objects sitting on/touching the selection
    // and add them to the selection so they move together as a group.
    GroupMoveResolver::AutoIncludeTouchingObjects(selections, s_groupMoveEnabled);

    // Re-fetch selection after potentially adding touching objects
    const auto& updatedSelections = selectionState->GetSelection();

    // Check if the first selected object is an NPC
    // NPC mode only supports single selection
    if (updatedSelections.size() == 1 && RemoteNPCPlacementManager::IsNPC(updatedSelections[0].ref)) {
        auto* actor = updatedSelections[0].ref->As<RE::Actor>();
        if (actor) {
            // Calculate initial distance from hand to NPC
            RE::NiPoint3 handPos = GetHandPosition();
            RE::NiPoint3 npcPos = actor->GetPosition();
            RE::NiPoint3 diff = {
                npcPos.x - handPos.x,
                npcPos.y - handPos.y,
                npcPos.z - handPos.z
            };
            float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
            if (distance < kMinDistance) {
                distance = kMinDistance;
            }

            // Delegate to NPC placement manager
            if (RemoteNPCPlacementManager::GetSingleton()->OnEnter(actor, distance)) {
                m_isNPCMode = true;
                m_isActive = true;
                m_distance = distance;
                m_centerPoint = npcPos;

                // Initialize smoother for position
                RE::NiTransform centerTransform;
                centerTransform.translate = m_centerPoint;
                centerTransform.rotate = RE::NiMatrix3();  // Identity
                centerTransform.scale = 1.0f;
                m_smoother.SetCurrent(centerTransform);

                // Show the ray
                UpdateRayVisualization();

                spdlog::info("RemoteGrabController: OnEnter in NPC mode for {:08X}", actor->GetFormID());
                return;
            }
        }
    }

    // Standard object mode
    m_isNPCMode = false;

    // Build our object list with initial transforms
    m_objects.clear();
    m_objects.reserve(updatedSelections.size());

    for (const auto& sel : updatedSelections) {
        if (!sel.ref || !sel.ref->Get3D()) {
            continue;
        }

        // Skip NPCs in multi-selection (only regular objects)
        if (RemoteNPCPlacementManager::IsNPC(sel.ref)) {
            spdlog::trace("RemoteGrabController::OnEnter: Skipping NPC {:08X} in multi-selection", sel.formId);
            continue;
        }

        RemoteGrabObject obj;
        obj.ref = sel.ref;
        obj.formId = sel.formId;
        obj.initialTransform = sel.ref->Get3D()->world;
        // Capture original Euler angles directly from game data (NOT converted from matrix)
        // This allows lossless finalization: finalAngles = initial + deltaZ
        obj.initialEulerAngles = sel.ref->GetAngle();
        // offsetFromCenter will be calculated after we know the center
        m_objects.push_back(obj);
    }

    if (m_objects.empty()) {
        spdlog::warn("RemoteGrabController::OnEnter: No valid 3D objects in selection");
        return;
    }

    // Disable collisions on grabbed objects to prevent physics interference during movement
    // Exception: If player is standing on an object, its collision stays enabled
    DisableCollisionsOnEnter();

    // Calculate the center point (z = min, x/y = average)
    m_centerPoint = CalculateSelectionCenter(m_objects);

    // Calculate and store each object's offset from the center
    for (auto& obj : m_objects) {
        obj.offsetFromCenter = {
            obj.initialTransform.translate.x - m_centerPoint.x,
            obj.initialTransform.translate.y - m_centerPoint.y,
            obj.initialTransform.translate.z - m_centerPoint.z
        };
        // Note: We use initialTransform.rotate directly (no Euler angle extraction)
        // to avoid lossy Matrix→Euler→Matrix conversion that causes rotation snapping
    }

    // Calculate initial distance from hand to center
    RE::NiPoint3 handPos = GetHandPosition();
    RE::NiPoint3 diff = {
        m_centerPoint.x - handPos.x,
        m_centerPoint.y - handPos.y,
        m_centerPoint.z - handPos.z
    };
    m_distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

    // Enforce minimum distance
    if (m_distance < kMinDistance) {
        m_distance = kMinDistance;
    }

    // Reset rotation
    m_zRotation = 0.0f;
    m_baseRotation = RE::NiMatrix3();  // Identity

    // Snap objects to current laser pointer position
    RE::NiPoint3 targetCenter = CalculateTargetPosition();

    // Calculate the delta from old center to new center
    RE::NiPoint3 delta = {
        targetCenter.x - m_centerPoint.x,
        targetCenter.y - m_centerPoint.y,
        targetCenter.z - m_centerPoint.z
    };

    // Move all objects by this delta (snapping to laser position)
    for (auto& obj : m_objects) {
        if (!obj.ref) continue;

        RE::NiTransform newTransform = obj.initialTransform;
        newTransform.translate.x += delta.x;
        newTransform.translate.y += delta.y;
        newTransform.translate.z += delta.z;

        ApplyTransformToObject(obj.ref, newTransform);
    }

    // Update stored center point
    m_centerPoint = targetCenter;

    // Initialize smoother with current center
    RE::NiTransform centerTransform;
    centerTransform.translate = m_centerPoint;
    centerTransform.rotate = m_baseRotation;
    centerTransform.scale = 1.0f;
    m_smoother.SetCurrent(centerTransform);

    // Reset ground snap toggle to OFF for each new grab
    // User must press B to enable ground snapping; it doesn't persist between grabs
    m_groundSnapEnabled = false;

    // Reset scale mode state
    m_scaleModeActive = false;
    m_accumulatedScaleMultiplier = 1.0f;

    // Reset left-hand transform state
    m_leftTriggerHeld = false;
    m_leftTriggerValue = 0.0f;
    m_rotationTransformer.Reset();
    m_leftHandStartStates.clear();
    m_leftHandUndoEntries.clear();

    // Reset snap controller state - will be initialized on first frame if snap mode is enabled
    m_snapController.Reset();

    m_isActive = true;

    // Show the ray
    UpdateRayVisualization();

    spdlog::info("RemoteGrabController: OnEnter with {} objects, distance={:.1f}",
        m_objects.size(), m_distance);

    // Log initial transforms of all grabbed objects
    Util::ActionLogger::LogHeader("RemoteGrab START", m_objects.size());
    for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
        const auto& obj = m_objects[i];
        Util::ActionLogger::LogSnapshot(i + 1, static_cast<int>(m_objects.size()), obj.formId,
            obj.initialTransform.translate, obj.initialEulerAngles, obj.initialTransform.scale);
    }
}

void RemoteGrabController::OnExit()
{
    if (!m_isActive) {
        return;
    }

    spdlog::info("RemoteGrabController: OnExit (NPC mode: {})", m_isNPCMode);

    // Hide the ray
    RaycastRenderer::Hide();

    if (m_isNPCMode) {
        // NPC mode - delegate exit to NPC placement manager
        auto npcResult = RemoteNPCPlacementManager::GetSingleton()->OnExit();

        // Record undo action if the NPC moved a meaningful distance
        if (npcResult) {
            float dx = npcResult->finalPosition.x - npcResult->initialPosition.x;
            float dy = npcResult->finalPosition.y - npcResult->initialPosition.y;
            float dz = npcResult->finalPosition.z - npcResult->initialPosition.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq >= 1.0f) {
                RE::NiTransform initial;
                initial.translate = npcResult->initialPosition;

                RE::NiTransform changed;
                changed.translate = npcResult->finalPosition;

                // NPCs handle their own rotation — store zero angles
                RE::NiPoint3 zeroAngles{0.0f, 0.0f, 0.0f};

                auto* history = Actions::ActionHistoryRepository::GetSingleton();
                history->AddTransform(npcResult->formId, initial, changed, zeroAngles, zeroAngles);
                spdlog::info("RemoteGrabController: Recorded NPC transform action for {:08X}", npcResult->formId);
            }
        }
    } else {
        // Standard object mode

        // If left-hand gesture is still in progress, finalize it first
        // (this handles the case where user releases right trigger while still holding left trigger)
        if (m_leftTriggerHeld) {
            FinishLeftHandRotation();
            m_leftTriggerHeld = false;
        }

        // Remove any left-hand undo entries before recording the final placement action
        // The rotation/translation is now included in the final placement, so we don't want duplicate undo entries
        RemoveLeftHandUndoEntries();

        // Restore collisions that were disabled during the grab
        RestoreCollisionsOnExit();

        // Log before/after transforms of all grabbed objects
        Util::ActionLogger::LogHeader("RemoteGrab END", m_objects.size());
        for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
            const auto& obj = m_objects[i];
            if (!obj.ref || !obj.ref->Get3D()) continue;
            RE::NiPoint3 currentPos = obj.ref->Get3D()->world.translate;
            RE::NiPoint3 currentEuler = obj.ref->GetAngle();
            float currentScale = obj.ref->Get3D()->world.scale;
            Util::ActionLogger::LogChange(i + 1, static_cast<int>(m_objects.size()), obj.formId,
                obj.initialTransform.translate, obj.initialEulerAngles, obj.initialTransform.scale,
                currentPos, currentEuler, currentScale);
        }

        // Record actions for undo
        RecordActions();

        // Finalize positions (sync Havok, etc.)
        FinalizePositions();
    }

    // Clear state
    m_isActive = false;
    m_isNPCMode = false;
    m_objects.clear();
    m_thumbstickX = 0.0f;
    m_thumbstickY = 0.0f;

    // Reset acceleration helper
    m_accelerationHelper.Reset();

    // Reset ground snap toggle
    m_groundSnapEnabled = false;

    // Reset scale mode state
    m_scaleModeActive = false;
    m_accumulatedScaleMultiplier = 1.0f;

    // Reset left-hand transform state
    m_leftTriggerHeld = false;
    m_leftTriggerValue = 0.0f;
    m_rotationTransformer.Reset();
    m_leftHandStartStates.clear();
    m_leftHandUndoEntries.clear();
}

void RemoteGrabController::OnFrameUpdate(float deltaTime)
{
    if (!m_isActive) {
        return;
    }

    if (m_isNPCMode) {
        UpdateNPCMode(deltaTime);
    } else {
        UpdateStandardMode(deltaTime);
    }

    UpdateRayVisualization();
}

void RemoteGrabController::ProcessThumbstickInput(float& outX, float& outY) const
{
    // Apply deadzone with axis isolation to prevent accidental dual-axis activation
    // Only the dominant axis (X or Y) is active; the other is zeroed
    float absX = std::abs(m_thumbstickX);
    float absY = std::abs(m_thumbstickY);

    outX = 0.0f;
    outY = 0.0f;

    if (absX > kThumbstickDeadzone || absY > kThumbstickDeadzone) {
        if (absX > absY) {
            // X-axis (rotation) is dominant - remap [deadzone, 1.0] to [0.0, 1.0]
            float sign = (m_thumbstickX > 0.0f) ? 1.0f : -1.0f;
            outX = sign * (absX - kThumbstickDeadzone) / (1.0f - kThumbstickDeadzone);
        } else {
            // Y-axis (distance) is dominant - remap [deadzone, 1.0] to [0.0, 1.0]
            float sign = (m_thumbstickY > 0.0f) ? 1.0f : -1.0f;
            outY = sign * (absY - kThumbstickDeadzone) / (1.0f - kThumbstickDeadzone);
        }
    }
}

void RemoteGrabController::UpdateNPCMode(float deltaTime)
{
    auto* npcManager = RemoteNPCPlacementManager::GetSingleton();
    if (!npcManager->IsActive()) {
        spdlog::warn("RemoteGrabController: NPC manager became inactive, exiting");
        OnExit();
        return;
    }

    // Only use Y-axis for distance (no rotation for NPCs)
    float stickX, stickY;
    ProcessThumbstickInput(stickX, stickY);

    // NPC mode: no rotation, distance only
    // Configure acceleration for NPC mode (no full-throttle requirement)
    ThumbstickAccelerationHelper::Config accelConfig;
    accelConfig.fastModeThreshold = kFastModeThreshold;
    accelConfig.farDistanceThreshold = kFarDistanceThreshold;
    accelConfig.slowdownDistance = kSlowdownDistance;
    accelConfig.fastMoveMultiplier = kFastMoveMultiplier;
    accelConfig.speedTransitionTime = kSpeedTransitionTime;
    accelConfig.requireFullThrottle = false;  // NPC mode: accelerate at any input level

    float speedMultiplier = m_accelerationHelper.Update(stickY, m_distance, deltaTime, accelConfig);

    // Update distance with accelerated speed
    m_distance += stickY * kMoveSpeed * speedMultiplier * deltaTime;
    if (m_distance < kMinDistance) {
        m_distance = kMinDistance;
    }

    // Calculate target position and smooth
    RE::NiTransform target;
    target.translate = CalculateTargetPosition();
    target.rotate = RE::NiMatrix3();  // Identity - no rotation for NPCs
    target.scale = 1.0f;

    m_smoother.SetTarget(target);
    m_smoother.Update(deltaTime);

    RE::NiTransform smoothed = m_smoother.GetCurrent();

    // Update NPC position via the manager
    npcManager->UpdatePosition(smoothed.translate);
    m_centerPoint = smoothed.translate;
}

void RemoteGrabController::UpdateStandardMode(float deltaTime)
{
    // Validate objects still exist
    bool anyInvalid = false;
    for (const auto& obj : m_objects) {
        if (!obj.ref || !obj.ref->Get3D()) {
            anyInvalid = true;
            break;
        }
    }
    if (anyInvalid || m_objects.empty()) {
        spdlog::warn("RemoteGrabController: Objects became invalid, exiting");
        OnExit();
        return;
    }

    // Update left-hand rotation transformer if trigger is held
    if (m_leftTriggerHeld) {
        m_rotationTransformer.Update(deltaTime);
    }

    // Process thumbstick input with deadzone and axis isolation
    float stickX, stickY;
    ProcessThumbstickInput(stickX, stickY);

    // Configure acceleration for standard mode (require full throttle)
    ThumbstickAccelerationHelper::Config accelConfig;
    accelConfig.fastModeThreshold = kFastModeThreshold;
    accelConfig.farDistanceThreshold = kFarDistanceThreshold;
    accelConfig.slowdownDistance = kSlowdownDistance;
    accelConfig.fastMoveMultiplier = kFastMoveMultiplier;
    accelConfig.speedTransitionTime = kSpeedTransitionTime;
    accelConfig.requireFullThrottle = true;
    accelConfig.fullThrottleThreshold = 0.92f;

    // When scale mode is active, Y axis controls scale instead of distance
    if (m_scaleModeActive) {
        // Update scale multiplier based on thumbstick Y
        // Positive Y (thumbstick up) = scale up, Negative Y (thumbstick down) = scale down
        float scaleChange = stickY * kScaleSpeed * deltaTime;
        m_accumulatedScaleMultiplier += scaleChange;

        // Clamp scale to reasonable bounds
        if (m_accumulatedScaleMultiplier < kMinScale) {
            m_accumulatedScaleMultiplier = kMinScale;
        } else if (m_accumulatedScaleMultiplier > kMaxScale) {
            m_accumulatedScaleMultiplier = kMaxScale;
        }
    } else {
        float speedMultiplier = m_accelerationHelper.Update(stickY, m_distance, deltaTime, accelConfig);

        // Update distance with accelerated speed
        m_distance += stickY * kMoveSpeed * speedMultiplier * deltaTime;
        if (m_distance < kMinDistance) {
            m_distance = kMinDistance;
        }
    }

    // Update Z rotation based on thumbstick X
    m_zRotation += stickX * kRotateSpeed * deltaTime;

    // Calculate target center position and rotation
    RE::NiTransform target;
    target.translate = CalculateTargetPosition();
    target.rotate = PositioningUtil::RotationAroundZ(m_zRotation);
    target.scale = 1.0f;

    // Smooth toward target
    m_smoother.SetTarget(target);
    m_smoother.Update(deltaTime);

    RE::NiTransform smoothed = m_smoother.GetCurrent();

    // Update all objects (pass deltaTime for snap motion smoothing)
    UpdateAllObjects(smoothed.translate, smoothed.rotate, deltaTime);
    m_centerPoint = smoothed.translate;
}

RE::NiPoint3 RemoteGrabController::GetHandPosition() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        return hand->world.translate;
    }
    return RE::NiPoint3{0, 0, 0};
}

RE::NiPoint3 RemoteGrabController::GetHandForward() const
{
    RE::NiAVObject* hand = VRNodes::GetRightHand();
    if (hand) {
        // The hand's forward direction is the Y axis of the rotation matrix
        const RE::NiMatrix3& rot = hand->world.rotate;
        return RE::NiPoint3{
            rot.entry[0][1],
            rot.entry[1][1],
            rot.entry[2][1]
        };
    }
    return RE::NiPoint3{0, 1, 0};
}

RE::NiPoint3 RemoteGrabController::CalculateSelectionCenter(const std::vector<RemoteGrabObject>& objects) const
{
    if (objects.empty()) {
        return RE::NiPoint3{0, 0, 0};
    }

    float sumX = 0.0f;
    float sumY = 0.0f;
    float minZ = (std::numeric_limits<float>::max)();  // Parens to avoid Windows max macro

    for (const auto& obj : objects) {
        const RE::NiPoint3& pos = obj.initialTransform.translate;
        sumX += pos.x;
        sumY += pos.y;
        if (pos.z < minZ) {
            minZ = pos.z;
        }
    }

    float count = static_cast<float>(objects.size());
    return RE::NiPoint3{
        sumX / count,  // Average X
        sumY / count,  // Average Y
        minZ           // Minimum Z (floor level)
    };
}

RE::NiPoint3 RemoteGrabController::CalculateTargetPosition() const
{
    RE::NiPoint3 handPos = GetHandPosition();
    RE::NiPoint3 forward = GetHandForward();

    // Normalize forward vector
    float len = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (len > 0.001f) {
        forward.x /= len;
        forward.y /= len;
        forward.z /= len;
    }

    // Position = hand + forward * distance
    return RE::NiPoint3{
        handPos.x + forward.x * m_distance,
        handPos.y + forward.y * m_distance,
        handPos.z + forward.z * m_distance
    };
}

void RemoteGrabController::ApplyTransformToObject(RE::TESObjectREFR* ref, const RE::NiTransform& transform)
{
    if (!ref) {
        return;
    }

    // Use the shared positioning utility
    PositioningUtil::ApplyTransformDuringGrab(ref, transform, true);
}

void RemoteGrabController::UpdateAllObjects(const RE::NiPoint3& centerPos, const RE::NiMatrix3& rotation, float deltaTime)
{
    // Extract the smoothed Z angle from the rotation matrix
    float rawAngle = ExtractZRotation(rotation);

    // Compute snapped/smoothed position and angle via snap controller
    // When snap mode is enabled, positions are snapped to grid then motion-smoothed
    // Rotation grid snapping is handled per-object in RemoteGrabTransformCalculator
    auto snapResult = m_snapController.ComputeSmoothedSnap(centerPos, rawAngle, deltaTime);
    RE::NiPoint3 displayCenter = snapResult.position;
    float smoothedAngle = snapResult.zAngle;

    // Get total left-hand rotation: accumulated (from previous gestures) + current (if active)
    // This is the key to preventing rotation from snapping back on release
    RE::NiMatrix3 totalLeftHandRotation = m_rotationTransformer.GetAccumulatedRotation();
    RE::NiPoint3 totalLeftHandEuler = m_rotationTransformer.GetAccumulatedEulerDelta();

    if (m_rotationTransformer.IsActive()) {
        // Right-multiply current gesture's local-frame delta onto accumulated
        RE::NiMatrix3 currentDelta = m_rotationTransformer.GetRotationDelta();
        totalLeftHandRotation = PositioningUtil::MultiplyMatrices(totalLeftHandRotation, currentDelta);

        RE::NiPoint3 currentEuler = m_rotationTransformer.GetEulerDelta();
        totalLeftHandEuler.x += currentEuler.x;
        totalLeftHandEuler.y += currentEuler.y;
        totalLeftHandEuler.z += currentEuler.z;
    }

    // Check if any left-hand rotation is applied
    bool hasLeftHandRotation = (std::abs(totalLeftHandEuler.x) > 0.001f ||
                                std::abs(totalLeftHandEuler.y) > 0.001f ||
                                std::abs(totalLeftHandEuler.z) > 0.001f);

    for (auto& obj : m_objects) {
        if (!obj.ref || !obj.ref->Get3D()) {
            continue;
        }

        // Physics items should never snap to ground - they need to stay floating
        // so they don't clip through surfaces or behave unexpectedly
        bool shouldSnapToGround = m_groundSnapEnabled && !IsClutterOrPhysicsObject(obj.ref);

        // Use centralized transform calculator with display (snapped+smoothed) values
        // When grid mode is enabled, the calculator snaps each object's FINAL world rotation
        // to the grid (e.g., 0°, 15°, 30°...) ensuring alignment with cell/world cardinal axes
        ComputedObjectTransform computed = RemoteGrabTransformCalculator::Calculate(
            obj, displayCenter, smoothedAngle, shouldSnapToGround,
            m_snapController.IsEnabled() && m_snapController.IsRotationSnappingEnabled(),
            m_snapController.GetRotationGridDegrees());

        // Store whether this object was ground-snapped for use in finalization
        obj.wasGroundSnapped = computed.groundSnapped;

        // Apply left-hand rotation if any (group rotation around center)
        if (hasLeftHandRotation) {
            // Rotate position around group center (using display center)
            RE::NiPoint3 offset = {
                computed.transform.translate.x - displayCenter.x,
                computed.transform.translate.y - displayCenter.y,
                computed.transform.translate.z - displayCenter.z
            };
            // Matrix * point multiplication
            RE::NiPoint3 rotatedOffset = {
                totalLeftHandRotation.entry[0][0] * offset.x + totalLeftHandRotation.entry[0][1] * offset.y + totalLeftHandRotation.entry[0][2] * offset.z,
                totalLeftHandRotation.entry[1][0] * offset.x + totalLeftHandRotation.entry[1][1] * offset.y + totalLeftHandRotation.entry[1][2] * offset.z,
                totalLeftHandRotation.entry[2][0] * offset.x + totalLeftHandRotation.entry[2][1] * offset.y + totalLeftHandRotation.entry[2][2] * offset.z
            };
            computed.transform.translate = {
                displayCenter.x + rotatedOffset.x,
                displayCenter.y + rotatedOffset.y,
                displayCenter.z + rotatedOffset.z
            };

            // Right-multiply: applies rotation in the object's local frame
            // so controller yaw/pitch/roll map 1:1 to object yaw/pitch/roll
            computed.transform.rotate = PositioningUtil::MultiplyMatrices(
                computed.transform.rotate, totalLeftHandRotation);

            // Extract Euler angles from the final rotation matrix
            // (Euler angles don't compose additively — must derive from resulting matrix)
            computed.eulerAngles = PositioningUtil::MatrixToEulerAngles(computed.transform.rotate);
        }

        // Apply scale multiplier on top of initial scale
        float finalScale = obj.initialTransform.scale * m_accumulatedScaleMultiplier;
        computed.transform.scale = finalScale;

        ApplyTransformToObject(obj.ref, computed.transform);

        // Update game's angle data so rotation persists through Update3DPosition()
        PositioningUtil::SetAngleNative(obj.ref, computed.eulerAngles);

        // Apply scale directly to the object
        obj.ref->SetScale(finalScale);
    }
}

void RemoteGrabController::UpdateRayVisualization()
{
    if (!m_isActive) {
        RaycastRenderer::Hide();
        return;
    }

    RE::NiPoint3 handPos = GetHandPosition();

    RaycastRenderer::LineParams line;
    line.start = handPos;
    line.end = m_centerPoint;

    if (RaycastRenderer::IsVisible()) {
        RaycastRenderer::Update(line, RaycastRenderer::BeamType::Default);
    } else {
        RaycastRenderer::Show(line, RaycastRenderer::BeamType::Default);
    }
}

void RemoteGrabController::RecordActions()
{
    std::vector<Actions::SingleTransform> transforms;
    transforms.reserve(m_objects.size());

    RE::NiTransform smoothed = m_smoother.GetCurrent();
    float smoothedAngle = ExtractZRotation(smoothed.rotate);

    // Get accumulated left-hand rotation (same as UpdateAllObjects)
    RE::NiMatrix3 totalLeftHandRotation = m_rotationTransformer.GetAccumulatedRotation();
    RE::NiPoint3 totalLeftHandEuler = m_rotationTransformer.GetAccumulatedEulerDelta();

    bool hasLeftHandRotation = (std::abs(totalLeftHandEuler.x) > 0.001f ||
                                std::abs(totalLeftHandEuler.y) > 0.001f ||
                                std::abs(totalLeftHandEuler.z) > 0.001f);

    for (const auto& obj : m_objects) {
        if (!obj.ref || !obj.ref->Get3D()) {
            continue;
        }

        // Use centralized transform calculator with per-object ground snap state
        // This preserves the snapped position/rotation that was shown during movement
        // Grid settings ensure final recorded rotation matches what user saw on screen
        ComputedObjectTransform computed = RemoteGrabTransformCalculator::Calculate(
            obj, smoothed.translate, smoothedAngle, obj.wasGroundSnapped,
            m_snapController.IsEnabled() && m_snapController.IsRotationSnappingEnabled(),
            m_snapController.GetRotationGridDegrees());

        // Apply accumulated left-hand rotation (group rotation around center)
        if (hasLeftHandRotation) {
            // Rotate position around group center
            RE::NiPoint3 offset = {
                computed.transform.translate.x - smoothed.translate.x,
                computed.transform.translate.y - smoothed.translate.y,
                computed.transform.translate.z - smoothed.translate.z
            };
            RE::NiPoint3 rotatedOffset = {
                totalLeftHandRotation.entry[0][0] * offset.x + totalLeftHandRotation.entry[0][1] * offset.y + totalLeftHandRotation.entry[0][2] * offset.z,
                totalLeftHandRotation.entry[1][0] * offset.x + totalLeftHandRotation.entry[1][1] * offset.y + totalLeftHandRotation.entry[1][2] * offset.z,
                totalLeftHandRotation.entry[2][0] * offset.x + totalLeftHandRotation.entry[2][1] * offset.y + totalLeftHandRotation.entry[2][2] * offset.z
            };
            computed.transform.translate = {
                smoothed.translate.x + rotatedOffset.x,
                smoothed.translate.y + rotatedOffset.y,
                smoothed.translate.z + rotatedOffset.z
            };

            // Right-multiply: applies rotation in the object's local frame
            computed.transform.rotate = PositioningUtil::MultiplyMatrices(
                computed.transform.rotate, totalLeftHandRotation);
            // Extract Euler angles from the final rotation matrix
            computed.eulerAngles = PositioningUtil::MatrixToEulerAngles(computed.transform.rotate);
        }

        // Apply scale multiplier on top of initial scale
        float finalScale = obj.initialTransform.scale * m_accumulatedScaleMultiplier;
        computed.transform.scale = finalScale;

        // Check if there was meaningful movement or scale change
        float distSq = Util::RotationMath::DistanceSquared(
            computed.transform.translate, obj.initialTransform.translate);
        float scaleChange = std::abs(finalScale - obj.initialTransform.scale);

        // Only include if movement was significant (> 1 unit) or scale changed
        if (distSq >= 1.0f || scaleChange > 0.001f) {
            Actions::SingleTransform st;
            st.formId = obj.formId;
            st.initialTransform = obj.initialTransform;
            st.changedTransform = computed.transform;
            st.initialEulerAngles = obj.initialEulerAngles;
            st.changedEulerAngles = computed.eulerAngles;

            transforms.push_back(st);
        }
    }

    if (transforms.empty()) {
        spdlog::trace("RemoteGrabController: No significant movement or scale change, skipping action record");
        return;
    }

    auto* history = Actions::ActionHistoryRepository::GetSingleton();

    if (transforms.size() == 1) {
        const auto& t = transforms[0];
        history->AddTransform(t.formId, t.initialTransform, t.changedTransform,
                              t.initialEulerAngles, t.changedEulerAngles);
        spdlog::info("RemoteGrabController: Recorded transform action for {:08X}", t.formId);
    } else {
        history->AddMultiTransform(std::move(transforms));
        spdlog::info("RemoteGrabController: Recorded multi-transform action for {} objects", transforms.size());
    }
}

void RemoteGrabController::FinalizePositions()
{
    RE::NiTransform smoothed = m_smoother.GetCurrent();
    float smoothedAngle = ExtractZRotation(smoothed.rotate);

    // Get accumulated left-hand rotation (same as UpdateAllObjects)
    RE::NiMatrix3 totalLeftHandRotation = m_rotationTransformer.GetAccumulatedRotation();
    RE::NiPoint3 totalLeftHandEuler = m_rotationTransformer.GetAccumulatedEulerDelta();

    bool hasLeftHandRotation = (std::abs(totalLeftHandEuler.x) > 0.001f ||
                                std::abs(totalLeftHandEuler.y) > 0.001f ||
                                std::abs(totalLeftHandEuler.z) > 0.001f);

    auto* deferredManager = DeferredCollisionUpdateManager::GetSingleton();

    for (const auto& obj : m_objects) {
        if (!obj.ref || !obj.ref->Get3D()) {
            continue;
        }

        // Use centralized transform calculator with per-object ground snap state
        // This preserves the snapped position/rotation that was shown during movement
        // Grid settings ensure finalized rotation matches what user saw on screen
        ComputedObjectTransform computed = RemoteGrabTransformCalculator::Calculate(
            obj, smoothed.translate, smoothedAngle, obj.wasGroundSnapped,
            m_snapController.IsEnabled() && m_snapController.IsRotationSnappingEnabled(),
            m_snapController.GetRotationGridDegrees());

        // Apply accumulated left-hand rotation (group rotation around center)
        if (hasLeftHandRotation) {
            // Rotate position around group center
            RE::NiPoint3 offset = {
                computed.transform.translate.x - smoothed.translate.x,
                computed.transform.translate.y - smoothed.translate.y,
                computed.transform.translate.z - smoothed.translate.z
            };
            RE::NiPoint3 rotatedOffset = {
                totalLeftHandRotation.entry[0][0] * offset.x + totalLeftHandRotation.entry[0][1] * offset.y + totalLeftHandRotation.entry[0][2] * offset.z,
                totalLeftHandRotation.entry[1][0] * offset.x + totalLeftHandRotation.entry[1][1] * offset.y + totalLeftHandRotation.entry[1][2] * offset.z,
                totalLeftHandRotation.entry[2][0] * offset.x + totalLeftHandRotation.entry[2][1] * offset.y + totalLeftHandRotation.entry[2][2] * offset.z
            };
            computed.transform.translate = {
                smoothed.translate.x + rotatedOffset.x,
                smoothed.translate.y + rotatedOffset.y,
                smoothed.translate.z + rotatedOffset.z
            };

            // Right-multiply: applies rotation in the object's local frame
            computed.transform.rotate = PositioningUtil::MultiplyMatrices(
                computed.transform.rotate, totalLeftHandRotation);
            // Extract Euler angles from the final rotation matrix
            computed.eulerAngles = PositioningUtil::MatrixToEulerAngles(computed.transform.rotate);
        }

        // Apply scale multiplier on top of initial scale
        float finalScale = obj.initialTransform.scale * m_accumulatedScaleMultiplier;
        computed.transform.scale = finalScale;

        // Sync game data with final position, angles, and scale
        obj.ref->SetPosition(computed.transform.translate);
        obj.ref->SetAngle(computed.eulerAngles);
        obj.ref->SetScale(finalScale);
        obj.ref->Update3DPosition(true);

        spdlog::info("RemoteGrabController: Finalized {:08X} with lossless Euler (deltaZ={:.3f}, groundSnapped={}, leftHandRot={}, scale={:.3f})",
            obj.formId, smoothedAngle, obj.wasGroundSnapped, hasLeftHandRotation, finalScale);

        // Check if player is standing on this object - if so, defer the collision update
        if (deferredManager->RegisterForDeferredUpdate(obj.ref, computed.transform)) {
            spdlog::info("RemoteGrabController: Deferring collision update for {:08X} (player standing on it)",
                obj.formId);
        } else {
            // Remove highlight before Disable/Enable destroys 3D - let scheduled refresh reapply it
            ObjectHighlighter::Unhighlight(obj.ref);

            // Disable/Enable cycle forces Havok to rebuild collision at new position
            obj.ref->Disable();
            obj.ref->Enable(false);

            // Schedule delayed highlight refresh after disable/enable destroys the 3D scene graph
            Selection::DelayedHighlightRefreshManager::GetSingleton()->ScheduleRefresh(obj.ref);
        }
    }
}

void RemoteGrabController::DisableCollisionsOnEnter()
{
    auto* deferredManager = DeferredCollisionUpdateManager::GetSingleton();

    for (auto& obj : m_objects) {
        if (!obj.ref || !obj.ref->Get3D()) {
            continue;
        }

        // Check if any actor (player or NPC) is standing on this object - if so, keep collision enabled
        // This prevents actors from falling through objects they're standing on
        if (deferredManager->IsAnyActorStandingOn(obj.ref)) {
            spdlog::info("RemoteGrabController: Keeping collision enabled for {:08X} (actor is standing on it)",
                obj.formId);
            obj.collisionDisabled = false;
            continue;
        }

        // No actor is on this object - safe to disable collision during the grab
        if (PositioningUtil::DisableCollision(obj.ref, obj.collisionState)) {
            obj.collisionDisabled = true;
            spdlog::info("RemoteGrabController: Disabled collision for {:08X} on grab enter",
                obj.formId);
        } else {
            obj.collisionDisabled = false;
        }
    }
}

void RemoteGrabController::RestoreCollisionsOnExit()
{
    for (auto& obj : m_objects) {
        if (!obj.ref || !obj.collisionDisabled) {
            continue;
        }

        // Restore collision to its original state
        PositioningUtil::RestoreCollision(obj.ref, obj.collisionState);
        spdlog::info("RemoteGrabController: Restored collision for {:08X} on grab exit",
            obj.formId);

        obj.collisionDisabled = false;
    }
}

bool RemoteGrabController::IsClutterOrPhysicsObject(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return false;
    }

    // Check if object has a collision layer indicating it's a physics/clutter object
    auto* node = ref->Get3D();
    if (!node) {
        return false;
    }

    // Check for bhkNiCollisionObject (Havok physics collision)
    auto* collision = node->GetCollisionObject();
    if (!collision) {
        return false;  // No collision = not a physics object
    }

    // Try to get the rigid body to check motion type
    auto* bhkCollision = skyrim_cast<RE::bhkCollisionObject*>(collision);
    if (!bhkCollision) {
        return false;
    }

    auto* rigidBody = bhkCollision->GetRigidBody();
    if (!rigidBody) {
        return false;
    }

    // Get the underlying Havok rigid body
    auto* hkBody = rigidBody->GetRigidBody();
    if (!hkBody) {
        return false;
    }

    // Check motion type - clutter objects are typically DYNAMIC or SPHERE_INERTIA
    auto motionType = hkBody->motion.type.get();

    // Motion types that indicate physics-enabled moveable objects:
    // - DYNAMIC: Fully simulated physics
    // - SPHERE_INERTIA: Simplified physics (common for clutter)
    // - BOX_INERTIA: Simplified physics (common for clutter)
    // - KEYFRAMED: Can be moved by animation/script
    if (motionType == RE::hkpMotion::MotionType::kDynamic ||
        motionType == RE::hkpMotion::MotionType::kSphereInertia ||
        motionType == RE::hkpMotion::MotionType::kBoxInertia) {
        return true;
    }

    // Also check collision layer - layer 4 (CLUTTER) and layer 5 (PROPS) are moveable
    auto layer = hkBody->collidable.GetCollisionLayer();
    if (layer == RE::COL_LAYER::kClutter || layer == RE::COL_LAYER::kProps) {
        return true;
    }

    return false;
}

void RemoteGrabController::StartLeftHandRotation()
{
    if (m_objects.empty()) {
        spdlog::warn("RemoteGrabController: StartLeftHandRotation called with no objects");
        return;
    }

    // Start rotation transformer (for left hand)
    m_rotationTransformer.Start(true);   // true = left hand

    // Capture per-object state at the start of this gesture
    // This is used for the undo entry's "initial" state
    m_leftHandStartStates.clear();
    m_leftHandStartStates.reserve(m_objects.size());

    for (const auto& obj : m_objects) {
        LeftHandGestureStartState state;
        if (obj.ref && obj.ref->Get3D()) {
            state.transform = obj.ref->Get3D()->world;
            state.eulerAngles = obj.ref->GetAngle();
        }
        m_leftHandStartStates.push_back(state);
    }

    spdlog::info("RemoteGrabController: Started left-hand gesture mode with {} objects", m_objects.size());
}

void RemoteGrabController::UpdateLeftHandRotation()
{
    // This function is now a no-op - transformers are updated directly in UpdateStandardMode
    // Kept for API compatibility
}

void RemoteGrabController::FinishLeftHandRotation()
{
    if (m_objects.empty() || m_leftHandStartStates.size() != m_objects.size()) {
        spdlog::warn("RemoteGrabController: FinishLeftHandRotation - object count mismatch or empty");
        m_leftHandStartStates.clear();
        m_rotationTransformer.Finish();
        return;
    }

    // Finish the rotation transformer - this "bakes in" the rotation
    // so it doesn't snap back when trigger is released
    m_rotationTransformer.Finish();

    // Check if any meaningful change occurred
    RE::NiPoint3 totalRotation = m_rotationTransformer.GetAccumulatedEulerDelta();
    float rotationMagnitude = std::abs(totalRotation.x) + std::abs(totalRotation.y) + std::abs(totalRotation.z);

    // Skip if changes were negligible
    if (rotationMagnitude < 0.02f) {
        spdlog::trace("RemoteGrabController: FinishLeftHandRotation - negligible changes, skipping undo entry");
        m_leftHandStartStates.clear();
        return;
    }

    // Build transform list for the undo action
    std::vector<Actions::SingleTransform> transforms;
    transforms.reserve(m_objects.size());

    for (size_t i = 0; i < m_objects.size(); ++i) {
        const auto& obj = m_objects[i];
        const auto& startState = m_leftHandStartStates[i];

        if (!obj.ref || !obj.ref->Get3D()) {
            continue;
        }

        // Get current state (after rotation/translation)
        RE::NiTransform currentTransform = obj.ref->Get3D()->world;
        RE::NiPoint3 currentEulers = obj.ref->GetAngle();

        Actions::SingleTransform st;
        st.formId = obj.formId;
        // Initial state is the state when this gesture STARTED
        st.initialTransform = startState.transform;
        st.initialEulerAngles = startState.eulerAngles;
        // Changed state is the current state (after the transform)
        st.changedTransform = currentTransform;
        st.changedEulerAngles = currentEulers;

        transforms.push_back(st);
    }

    if (transforms.empty()) {
        spdlog::trace("RemoteGrabController: FinishLeftHandRotation - no valid transforms to record");
        m_leftHandStartStates.clear();
        return;
    }

    // Record the action to history
    auto* history = Actions::ActionHistoryRepository::GetSingleton();
    Util::ActionId actionId;

    if (transforms.size() == 1) {
        const auto& t = transforms[0];
        actionId = history->AddTransform(t.formId, t.initialTransform, t.changedTransform,
                                          t.initialEulerAngles, t.changedEulerAngles);
        spdlog::info("RemoteGrabController: Recorded left-hand undo entry for {:08X}, actionId={:016X}",
            t.formId, actionId.Value());
    } else {
        actionId = history->AddMultiTransform(std::move(transforms));
        spdlog::info("RemoteGrabController: Recorded multi-object left-hand undo entry, actionId={:016X}",
            actionId.Value());
    }

    // Track this action ID for removal when exiting remote mode
    m_leftHandUndoEntries.push_back(actionId);

    // Reset state for next gesture
    m_leftHandStartStates.clear();

    spdlog::info("RemoteGrabController: Finished left-hand gesture (rotation: x={:.3f}, y={:.3f}, z={:.3f})",
        totalRotation.x, totalRotation.y, totalRotation.z);
}

void RemoteGrabController::RemoveLeftHandUndoEntries()
{
    if (m_leftHandUndoEntries.empty()) {
        return;
    }

    auto* history = Actions::ActionHistoryRepository::GetSingleton();

    // Remove all left-hand undo entries (they're now included in the final placement action)
    for (const auto& actionId : m_leftHandUndoEntries) {
        if (history->Remove(actionId)) {
            spdlog::info("RemoteGrabController: Removed left-hand undo entry {:016X}", actionId.Value());
        } else {
            spdlog::warn("RemoteGrabController: Failed to remove left-hand undo entry {:016X}", actionId.Value());
        }
    }

    m_leftHandUndoEntries.clear();
}

} // namespace Grab
