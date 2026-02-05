#include "RotationMath.h"
#include <cmath>

namespace Util::RotationMath {

float ExtractZRotation(const RE::NiMatrix3& m)
{
    // For Skyrim's row-vector convention (SetEulerAnglesXYZ):
    // entry[0][0] = cosY * cosZ
    // entry[0][1] = cosY * sinZ
    // So: atan2(entry[0][1], entry[0][0]) = atan2(sinZ, cosZ) = Z
    return std::atan2(m.entry[0][1], m.entry[0][0]);
}

RE::NiMatrix3 EulerToMatrix(const RE::NiPoint3& angles)
{
    float cx = std::cos(angles.x);
    float sx = std::sin(angles.x);
    float cy = std::cos(angles.y);
    float sy = std::sin(angles.y);
    float cz = std::cos(angles.z);
    float sz = std::sin(angles.z);

    RE::NiMatrix3 result;
    // Match Skyrim's NiMatrix3::SetEulerAnglesXYZ exactly
    // This is the row-vector convention used by the engine
    result.entry[0][0] = cy * cz;
    result.entry[0][1] = cy * sz;
    result.entry[0][2] = -sy;

    result.entry[1][0] = sx * sy * cz - cx * sz;
    result.entry[1][1] = sx * sy * sz + cx * cz;
    result.entry[1][2] = sx * cy;

    result.entry[2][0] = cx * sy * cz + sx * sz;
    result.entry[2][1] = cx * sy * sz - sx * cz;
    result.entry[2][2] = cx * cy;

    return result;
}

RE::NiPoint3 RotatePointAroundZ(const RE::NiPoint3& point, const RE::NiPoint3& origin, float angle)
{
    float c = std::cos(angle);
    float s = std::sin(angle);

    // Translate to origin
    float dx = point.x - origin.x;
    float dy = point.y - origin.y;

    // Rotate
    float rx = dx * c - dy * s;
    float ry = dx * s + dy * c;

    // Translate back
    return RE::NiPoint3{
        origin.x + rx,
        origin.y + ry,
        point.z  // Z unchanged for rotation around Z axis
    };
}

float Normalize(RE::NiPoint3& v)
{
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.001f) {
        v.x /= len;
        v.y /= len;
        v.z /= len;
    }
    return len;
}

float Distance(const RE::NiPoint3& a, const RE::NiPoint3& b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;
    return dx * dx + dy * dy + dz * dz;
}

} // namespace Util::RotationMath
