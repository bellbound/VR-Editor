#pragma once

#include <RE/Skyrim.h>

namespace PositioningUtil {

// Native engine functions for position/angle updates (from OMO)
// These properly update Havok collision bodies, unlike MoveTo_Impl
void SetPositionNative(RE::TESObjectREFR* ref, const RE::NiPoint3& position);
void SetAngleNative(RE::TESObjectREFR* ref, const RE::NiPoint3& angle);

// Convert NiMatrix3 rotation to Euler angles (radians)
// Returns angles in the order expected by Skyrim: (X/pitch, Y/roll, Z/yaw)
// WARNING: This conversion is inherently lossy due to gimbal lock and multiple
// representations. Prefer SurfaceNormalToEuler() for surface alignment.
RE::NiPoint3 MatrixToEulerAngles(const RE::NiMatrix3& m);

// Convert surface normal to Euler angles while preserving a specific yaw
// This is LOSSLESS for the yaw component, avoiding Matrix→Euler conversion.
//
// For ground-snapping operations where we want to align an object to a surface
// while preserving its horizontal facing direction (yaw):
//   - pitch/roll are derived directly from the surface normal
//   - yaw is passed through unchanged (from the original Euler + delta)
//
// Parameters:
//   normal: The surface normal (should be normalized)
//   preservedYaw: The yaw angle to preserve (in radians)
//
// Returns Euler angles (pitch, roll, yaw) that align the object's Z-axis
// with the surface normal while maintaining the specified yaw.
RE::NiPoint3 SurfaceNormalToEuler(const RE::NiPoint3& normal, float preservedYaw);

// Build a rotation matrix for rotation around Z axis
RE::NiMatrix3 RotationAroundZ(float angle);

// Multiply two matrices
RE::NiMatrix3 MultiplyMatrices(const RE::NiMatrix3& a, const RE::NiMatrix3& b);

// Check if a collision layer is considered "static" (should be disabled during grab)
bool IsStaticCollisionLayer(RE::COL_LAYER layer);

// Collision state tracking for grab operations
struct CollisionState {
    RE::COL_LAYER originalLayer = RE::COL_LAYER::kUnidentified;
    bool wasDisabled = false;
};

// Disable collision on a static object during grab (prevents physics interference)
// Returns true if collision was disabled, false if object was not static
bool DisableCollision(RE::TESObjectREFR* ref, CollisionState& outState);

// Restore collision on an object after release
void RestoreCollision(RE::TESObjectREFR* ref, CollisionState& state);

// Apply transform during grab (while collision may be disabled)
// updateRotation: false for position-only updates (single-hand grab)
void ApplyTransformDuringGrab(RE::TESObjectREFR* ref, const RE::NiTransform& transform, bool updateRotation = true);

// Finalize position after restoring collision on static objects
// This is the key fix: Havok needs a position update AFTER collision is restored
// to properly sync the collision body with the visual mesh
void FinalizeStaticObjectPosition(RE::TESObjectREFR* ref, const RE::NiTransform& transform);

// Full MoveTo implementation for cell transitions (from OMO)
// This is the "nuclear option" that fully resets Havok state
void MoveTo(RE::TESObjectREFR* ref, RE::TESObjectCELL* cell, RE::TESWorldSpace* worldspace,
            const RE::NiPoint3& position, const RE::NiPoint3& angle);

} // namespace PositioningUtil
