#pragma once

#include <RE/Skyrim.h>

namespace Util::RotationMath {

// Extract Z rotation angle from a rotation matrix
// For a pure Z rotation matrix, this gives the exact angle
float ExtractZRotation(const RE::NiMatrix3& m);

// Build rotation matrix from Euler angles (Skyrim's ZYX order)
// angles.x = pitch (X rotation), angles.y = roll (Y rotation), angles.z = yaw (Z rotation)
RE::NiMatrix3 EulerToMatrix(const RE::NiPoint3& angles);

// Rotate a point around the Z axis at a given origin
RE::NiPoint3 RotatePointAroundZ(const RE::NiPoint3& point, const RE::NiPoint3& origin, float angle);

// Normalize a vector in-place, returns original length
float Normalize(RE::NiPoint3& v);

// Calculate distance between two points
float Distance(const RE::NiPoint3& a, const RE::NiPoint3& b);

// Calculate squared distance between two points (faster, no sqrt)
float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b);

} // namespace Util::RotationMath
