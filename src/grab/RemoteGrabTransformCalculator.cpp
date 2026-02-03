#include "RemoteGrabTransformCalculator.h"
#include "RemoteGrabController.h"
#include "../util/RotationMath.h"
#include "../util/PositioningUtil.h"
#include "../util/Raycast.h"
#include <cmath>

namespace Grab {

RE::NiPoint3 RemoteGrabTransformCalculator::CalculateRotatedOffset(
    const RE::NiPoint3& offset,
    float angle)
{
    RE::NiPoint3 rotated = Util::RotationMath::RotatePointAroundZ(
        RE::NiPoint3{offset.x, offset.y, 0},
        RE::NiPoint3{0, 0, 0},
        angle
    );
    rotated.z = offset.z;
    return rotated;
}

RE::NiPoint3 RemoteGrabTransformCalculator::CalculateFloatingPosition(
    const RemoteGrabObject& obj,
    const RE::NiPoint3& centerPos,
    float smoothedAngle)
{
    RE::NiPoint3 rotatedOffset = CalculateRotatedOffset(obj.offsetFromCenter, smoothedAngle);

    return RE::NiPoint3{
        centerPos.x + rotatedOffset.x,
        centerPos.y + rotatedOffset.y,
        centerPos.z + rotatedOffset.z
    };
}

ComputedObjectTransform RemoteGrabTransformCalculator::Calculate(
    const RemoteGrabObject& obj,
    const RE::NiPoint3& centerPos,
    float smoothedAngle,
    bool groundSnapEnabled,
    bool rotationGridEnabled,
    float rotationGridDegrees)
{
    RE::NiPoint3 floatingPos = CalculateFloatingPosition(obj, centerPos, smoothedAngle);

    if (groundSnapEnabled) {
        return CalculateWithGroundSnap(obj, floatingPos, smoothedAngle, rotationGridEnabled, rotationGridDegrees);
    } else {
        return CalculateFloating(obj, floatingPos, smoothedAngle, rotationGridEnabled, rotationGridDegrees);
    }
}

float RemoteGrabTransformCalculator::SnapAngleToWorldGrid(float angleRad, float gridDegrees)
{
    float gridRad = gridDegrees * (3.14159265f / 180.0f);
    return std::round(angleRad / gridRad) * gridRad;
}

ComputedObjectTransform RemoteGrabTransformCalculator::CalculateFloating(
    const RemoteGrabObject& obj,
    const RE::NiPoint3& floatingPos,
    float smoothedAngle,
    bool rotationGridEnabled,
    float rotationGridDegrees)
{
    ComputedObjectTransform result;
    result.groundSnapped = false;

    result.transform.scale = obj.initialTransform.scale;
    result.transform.translate = floatingPos;

    // Calculate the final Z rotation (world-space yaw)
    float finalZ = obj.initialEulerAngles.z - smoothedAngle;

    // When rotation grid is enabled, snap the FINAL world rotation to the grid
    // This ensures objects align to cardinal axes (0°, 15°, 30°, etc.) regardless of their initial orientation
    if (rotationGridEnabled) {
        finalZ = SnapAngleToWorldGrid(finalZ, rotationGridDegrees);
    }

    // Compute the effective delta needed to achieve the (possibly snapped) final rotation
    float effectiveDelta = obj.initialEulerAngles.z - finalZ;

    // Apply Z rotation via matrix multiplication to preserve original orientation exactly
    // This avoids lossy Matrix->Euler->Matrix round-trip that causes rotation snapping
    // R_new = R_z(-effectiveDelta) * R_original
    // Note: Use NEGATIVE delta to rotate objects in the intuitive direction
    RE::NiMatrix3 deltaRotation = PositioningUtil::RotationAroundZ(-effectiveDelta);
    result.transform.rotate = PositioningUtil::MultiplyMatrices(deltaRotation, obj.initialTransform.rotate);

    // Lossless Euler angles for game data update
    result.eulerAngles = RE::NiPoint3{
        obj.initialEulerAngles.x,    // Pitch unchanged
        obj.initialEulerAngles.y,    // Roll unchanged
        finalZ                       // Yaw: snapped to world grid if enabled
    };

    return result;
}

ComputedObjectTransform RemoteGrabTransformCalculator::CalculateWithGroundSnap(
    const RemoteGrabObject& obj,
    const RE::NiPoint3& floatingPos,
    float smoothedAngle,
    bool rotationGridEnabled,
    float rotationGridDegrees)
{
    // Cast ray straight down from floating position
    const RE::NiPoint3 downDirection = {0.0f, 0.0f, -1.0f};
    RaycastResult rayResult = Raycast::CastRay(floatingPos, downDirection, RemoteGrabController::kGroundRayMaxDistance);

    if (!rayResult.hit) {
        // No ground found - fall back to floating transform
        return CalculateFloating(obj, floatingPos, smoothedAngle, rotationGridEnabled, rotationGridDegrees);
    }

    ComputedObjectTransform result;
    result.groundSnapped = true;

    result.transform.scale = obj.initialTransform.scale;
    result.transform.translate = rayResult.hitPoint;

    // Normalize surface normal
    RE::NiPoint3 normal = rayResult.hitNormal;
    float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (len > 0.001f) {
        normal.x /= len;
        normal.y /= len;
        normal.z /= len;
    } else {
        // Default to world up if normal is invalid
        normal = {0.0f, 0.0f, 1.0f};
    }

    // Calculate the final Z rotation (world-space yaw)
    float finalYaw = obj.initialEulerAngles.z - smoothedAngle;

    // When rotation grid is enabled, snap the FINAL world yaw to the grid
    // This ensures objects align to cardinal axes regardless of their initial orientation
    if (rotationGridEnabled) {
        finalYaw = SnapAngleToWorldGrid(finalYaw, rotationGridDegrees);
    }

    // LOSSLESS EULER PATH: Compute Euler angles directly from surface normal
    // - Pitch and roll come from the surface normal (how tilted the ground is)
    // - Yaw is snapped to world grid if enabled
    result.eulerAngles = PositioningUtil::SurfaceNormalToEuler(normal, finalYaw);

    // Build rotation matrix from these Euler angles for visual updates
    result.transform.rotate = Util::RotationMath::EulerToMatrix(result.eulerAngles);

    return result;
}

} // namespace Grab
