#include "PositioningUtil.h"
#include "../log.h"
#include <cmath>

namespace PositioningUtil {

void SetPositionNative(RE::TESObjectREFR* ref, const RE::NiPoint3& position)
{
    if (!ref) return;
    using func_t = void(RE::TESObjectREFR*, const RE::NiPoint3&);
    REL::Relocation<func_t> func{RELOCATION_ID(19363, 19790)};
    func(ref, position);
}

void SetAngleNative(RE::TESObjectREFR* ref, const RE::NiPoint3& angle)
{
    if (!ref) return;
    using func_t = void(RE::TESObjectREFR*, const RE::NiPoint3&);
    REL::Relocation<func_t> func{RELOCATION_ID(19359, 19786)};
    func(ref, angle);
}

RE::NiPoint3 MatrixToEulerAngles(const RE::NiMatrix3& m)
{
    RE::NiPoint3 angles;

    // Extract Euler angles from rotation matrix
    // Skyrim uses ZYX rotation order (yaw, pitch, roll)
    float sy = std::sqrt(m.entry[0][0] * m.entry[0][0] + m.entry[1][0] * m.entry[1][0]);
    bool singular = sy < 1e-6f;

    if (!singular) {
        angles.x = std::atan2(m.entry[2][1], m.entry[2][2]);  // Pitch (X)
        angles.y = std::atan2(-m.entry[2][0], sy);             // Roll (Y)
        angles.z = std::atan2(m.entry[1][0], m.entry[0][0]);   // Yaw (Z)
    } else {
        angles.x = std::atan2(-m.entry[1][2], m.entry[1][1]);
        angles.y = std::atan2(-m.entry[2][0], sy);
        angles.z = 0.0f;
    }

    return angles;
}

RE::NiPoint3 SurfaceNormalToEuler(const RE::NiPoint3& normal, float preservedYaw)
{
    // Derive pitch and roll directly from surface normal, preserving yaw.
    //
    // Mathematical derivation:
    // For Skyrim's ZYX Euler order, the rotation matrix R = Rx(pitch) * Ry(roll) * Rz(yaw).
    // The Z-column of R (column 2) is the direction the object's local Z-axis points.
    // We want this to equal the surface normal.
    //
    // For R = Rx(p) * Ry(r) (ignoring yaw which only affects XY orientation):
    // Z-column = [sin(r), -sin(p)*cos(r), cos(p)*cos(r)]
    //
    // Given normal = (nx, ny, nz), we solve:
    //   sin(r) = nx
    //   -sin(p)*cos(r) = ny
    //   cos(p)*cos(r) = nz
    //
    // From the normal being unit length: nx² + ny² + nz² = 1
    // So cos²(r) = 1 - sin²(r) = 1 - nx² = ny² + nz²
    //
    // Therefore:
    //   roll  = atan2(nx, sqrt(ny² + nz²))
    //   pitch = atan2(-ny, nz)  [when cos(r) ≠ 0]
    //
    // This decomposition is stable except when the normal is nearly horizontal
    // (pointing in ±X direction), which is a gimbal lock case we handle specially.

    RE::NiPoint3 angles;

    float nx = normal.x;
    float ny = normal.y;
    float nz = normal.z;

    // cos²(roll) = ny² + nz²
    float cosRollSq = ny * ny + nz * nz;
    float cosRoll = std::sqrt(cosRollSq);

    if (cosRoll > 1e-6f) {
        // Normal case: surface is not a vertical wall facing ±X
        angles.y = std::atan2(nx, cosRoll);     // Roll (Y rotation)
        angles.x = std::atan2(-ny, nz);         // Pitch (X rotation)
    } else {
        // Gimbal lock: surface normal is nearly horizontal (pointing ±X)
        // In this case, pitch and yaw become coupled. We preserve yaw and set pitch=0.
        angles.y = (nx > 0) ? (3.14159265f / 2.0f) : (-3.14159265f / 2.0f);  // ±90° roll
        angles.x = 0.0f;
    }

    // Yaw is preserved from the original Euler angles + any delta
    // This is the LOSSLESS part - we never convert yaw through a matrix
    angles.z = preservedYaw;

    return angles;
}

RE::NiMatrix3 RotationAroundZ(float angle)
{
    RE::NiMatrix3 result;
    float c = std::cos(angle);
    float s = std::sin(angle);

    // Match Skyrim's row-vector convention (same as NiMatrix3::SetEulerAnglesXYZ)
    // For Z-only rotation: entry[0][1] = +sin, entry[1][0] = -sin
    result.entry[0][0] = c;
    result.entry[0][1] = s;
    result.entry[0][2] = 0.0f;

    result.entry[1][0] = -s;
    result.entry[1][1] = c;
    result.entry[1][2] = 0.0f;

    result.entry[2][0] = 0.0f;
    result.entry[2][1] = 0.0f;
    result.entry[2][2] = 1.0f;

    return result;
}

RE::NiMatrix3 MultiplyMatrices(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
{
    RE::NiMatrix3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.entry[i][j] =
                a.entry[i][0] * b.entry[0][j] +
                a.entry[i][1] * b.entry[1][j] +
                a.entry[i][2] * b.entry[2][j];
        }
    }
    return result;
}

bool IsStaticCollisionLayer(RE::COL_LAYER layer)
{
    switch (layer) {
        case RE::COL_LAYER::kUnidentified:
        case RE::COL_LAYER::kStatic:
        case RE::COL_LAYER::kTrees:
        case RE::COL_LAYER::kAnimStatic:
        case RE::COL_LAYER::kTerrain:
        case RE::COL_LAYER::kTrap:
        case RE::COL_LAYER::kGround:
        case RE::COL_LAYER::kPortal:
            return true;
        default:
            return false;
    }
}

bool DisableCollision(RE::TESObjectREFR* ref, CollisionState& outState)
{
    outState.wasDisabled = false;
    outState.originalLayer = RE::COL_LAYER::kUnidentified;

    if (!ref) return false;

    auto* node = ref->Get3D();
    if (!node) return false;

    outState.originalLayer = node->GetCollisionLayer();

    if (IsStaticCollisionLayer(outState.originalLayer)) {
        node->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);
        outState.wasDisabled = true;
        spdlog::info("PositioningUtil: Disabled collision on {:08X} (was layer {})",
            ref->GetFormID(), static_cast<int>(outState.originalLayer));
        return true;
    }

    spdlog::trace("PositioningUtil: {:08X} layer {} is not static, skipping collision disable",
        ref->GetFormID(), static_cast<int>(outState.originalLayer));
    return false;
}

void RestoreCollision(RE::TESObjectREFR* ref, CollisionState& state)
{
    if (!ref || !state.wasDisabled) return;

    auto* node = ref->Get3D();
    if (!node) return;

    auto currentLayer = node->GetCollisionLayer();
    if (currentLayer != state.originalLayer) {
        node->SetCollisionLayer(state.originalLayer);
        spdlog::info("PositioningUtil: Restored collision on {:08X} to layer {}",
            ref->GetFormID(), static_cast<int>(state.originalLayer));
    }

    state.wasDisabled = false;
}

void ApplyTransformDuringGrab(RE::TESObjectREFR* ref, const RE::NiTransform& transform, bool updateRotation)
{
    if (!ref) return;

    auto* node = ref->Get3D();
    if (!node) return;

    // Update position via native function
    SetPositionNative(ref, transform.translate);

    // Sync the 3D scene node position with game data - this triggers proper render updates
    // IMPORTANT: This must be called BEFORE setting rotation, because it overwrites the
    // entire node transform from game data!
    ref->Update3DPosition(true);

    // CRITICAL: Set rotation AFTER Update3DPosition to avoid it being overwritten.
    // We set rotation directly on the NiNode to avoid Matrix→Euler→Matrix round-trip.
    // The lossy Euler conversion causes rotation snapping/flipping due to:
    //   - atan2 quadrant ambiguity
    //   - Multiple Euler representations for the same rotation
    //   - Sign convention mismatches with the engine
    //
    // We only convert to Euler in FinalizeStaticObjectPosition at the END of the grab.
    if (updateRotation) {
        node->world.rotate = transform.rotate;
    }

   
}

void FinalizeStaticObjectPosition(RE::TESObjectREFR* ref, const RE::NiTransform& transform)
{
    if (!ref) return;

    // KEY FIX: After restoring collision, we need to do a final position update
    // to sync the Havok collision body with the visual mesh.
    //
    // OMO uses ref->SetPosition(ref->GetPosition()) which calls the CommonLib
    // wrapper that does additional Havok sync. We replicate this behavior.
    //
    // The native SetPositionNative alone doesn't fully sync Havok when collision
    // was previously disabled.

    RE::NiPoint3 angles = MatrixToEulerAngles(transform.rotate);

    // Use the CommonLib SetPosition/SetAngle which may do additional sync
    ref->SetPosition(transform.translate);
    ref->SetAngle(angles);

    // Final 3D position update
    ref->Update3DPosition(true);

    spdlog::info("PositioningUtil: Finalized static object {:08X} position for Havok sync",
        ref->GetFormID());
}

void MoveTo(RE::TESObjectREFR* ref, RE::TESObjectCELL* cell, RE::TESWorldSpace* worldspace,
            const RE::NiPoint3& position, const RE::NiPoint3& angle)
{
    if (!ref) return;

    using func_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*,
                        RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
    REL::Relocation<func_t> func{RE::Offset::TESObjectREFR::MoveTo};
    func(ref, RE::ObjectRefHandle(), cell, worldspace, position, angle);
}

} // namespace PositioningUtil
