#include "TransformSmoother.h"
#include <cmath>

namespace Grab {

// === Quaternion helper for rotation interpolation ===
// NiMatrix3 uses rotation matrices, but slerp requires quaternions

struct Quat {
    float w, x, y, z;

    static Quat FromMatrix(const RE::NiMatrix3& m) {
        Quat q;
        float trace = m.entry[0][0] + m.entry[1][1] + m.entry[2][2];

        if (trace > 0.0f) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (m.entry[2][1] - m.entry[1][2]) / s;
            q.y = (m.entry[0][2] - m.entry[2][0]) / s;
            q.z = (m.entry[1][0] - m.entry[0][1]) / s;
        } else if (m.entry[0][0] > m.entry[1][1] && m.entry[0][0] > m.entry[2][2]) {
            float s = std::sqrt(1.0f + m.entry[0][0] - m.entry[1][1] - m.entry[2][2]) * 2.0f;
            q.w = (m.entry[2][1] - m.entry[1][2]) / s;
            q.x = 0.25f * s;
            q.y = (m.entry[0][1] + m.entry[1][0]) / s;
            q.z = (m.entry[0][2] + m.entry[2][0]) / s;
        } else if (m.entry[1][1] > m.entry[2][2]) {
            float s = std::sqrt(1.0f + m.entry[1][1] - m.entry[0][0] - m.entry[2][2]) * 2.0f;
            q.w = (m.entry[0][2] - m.entry[2][0]) / s;
            q.x = (m.entry[0][1] + m.entry[1][0]) / s;
            q.y = 0.25f * s;
            q.z = (m.entry[1][2] + m.entry[2][1]) / s;
        } else {
            float s = std::sqrt(1.0f + m.entry[2][2] - m.entry[0][0] - m.entry[1][1]) * 2.0f;
            q.w = (m.entry[1][0] - m.entry[0][1]) / s;
            q.x = (m.entry[0][2] + m.entry[2][0]) / s;
            q.y = (m.entry[1][2] + m.entry[2][1]) / s;
            q.z = 0.25f * s;
        }

        return q;
    }

    RE::NiMatrix3 ToMatrix() const {
        RE::NiMatrix3 m;
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        m.entry[0][0] = 1.0f - 2.0f * (yy + zz);
        m.entry[0][1] = 2.0f * (xy - wz);
        m.entry[0][2] = 2.0f * (xz + wy);

        m.entry[1][0] = 2.0f * (xy + wz);
        m.entry[1][1] = 1.0f - 2.0f * (xx + zz);
        m.entry[1][2] = 2.0f * (yz - wx);

        m.entry[2][0] = 2.0f * (xz - wy);
        m.entry[2][1] = 2.0f * (yz + wx);
        m.entry[2][2] = 1.0f - 2.0f * (xx + yy);

        return m;
    }

    Quat Normalize() const {
        float len = std::sqrt(w*w + x*x + y*y + z*z);
        if (len < 0.0001f) return Quat{1, 0, 0, 0};
        return Quat{w/len, x/len, y/len, z/len};
    }

    float Dot(const Quat& other) const {
        return w*other.w + x*other.x + y*other.y + z*other.z;
    }

    static Quat Slerp(const Quat& a, const Quat& b, float t) {
        Quat result;
        float dot = a.Dot(b);

        // If dot is negative, negate one quaternion to take shortest path
        Quat b2 = b;
        if (dot < 0.0f) {
            dot = -dot;
            b2.w = -b2.w;
            b2.x = -b2.x;
            b2.y = -b2.y;
            b2.z = -b2.z;
        }

        // If quaternions are very close, use linear interpolation
        if (dot > 0.9995f) {
            result.w = a.w + t * (b2.w - a.w);
            result.x = a.x + t * (b2.x - a.x);
            result.y = a.y + t * (b2.y - a.y);
            result.z = a.z + t * (b2.z - a.z);
            return result.Normalize();
        }

        float theta_0 = std::acos(dot);
        float theta = theta_0 * t;
        float sin_theta = std::sin(theta);
        float sin_theta_0 = std::sin(theta_0);

        float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
        float s1 = sin_theta / sin_theta_0;

        result.w = s0 * a.w + s1 * b2.w;
        result.x = s0 * a.x + s1 * b2.x;
        result.y = s0 * a.y + s1 * b2.y;
        result.z = s0 * a.z + s1 * b2.z;

        return result.Normalize();
    }
};

// === TransformSmoother Implementation ===

void TransformSmoother::SetTarget(const RE::NiTransform& target)
{
    m_target = target;
    m_isTransitioning = true;
}

void TransformSmoother::SetCurrent(const RE::NiTransform& current)
{
    m_current = current;
    m_target = current;  // Also set target to prevent immediate jump
    m_isTransitioning = false;
}

bool TransformSmoother::Update(float deltaTime)
{
    if (!m_isTransitioning) {
        return false;
    }

    // Validate deltaTime
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return false;
    }

    // Cap deltaTime to avoid excessive jumps after long pauses
    constexpr float MAX_DELTA = 0.1f;
    if (deltaTime > MAX_DELTA) {
        deltaTime = MAX_DELTA;
    }

    // Exponential smoothing factor
    float t = 1.0f - std::exp(-m_speed * deltaTime);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // Interpolate position
    m_current.translate = LerpPosition(m_current.translate, m_target.translate, t);

    // Interpolate rotation
    m_current.rotate = LerpRotation(m_current.rotate, m_target.rotate, t);

    // Interpolate scale
    m_current.scale += (m_target.scale - m_current.scale) * t;

    return true;
}

void TransformSmoother::Reset()
{
    m_isTransitioning = false;
    m_target = RE::NiTransform();
    m_current = RE::NiTransform();
}

RE::NiPoint3 TransformSmoother::LerpPosition(const RE::NiPoint3& a, const RE::NiPoint3& b, float t)
{
    return RE::NiPoint3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

RE::NiMatrix3 TransformSmoother::LerpRotation(const RE::NiMatrix3& a, const RE::NiMatrix3& b, float t)
{
    Quat qa = Quat::FromMatrix(a);
    Quat qb = Quat::FromMatrix(b);
    Quat result = Quat::Slerp(qa, qb, t);
    return result.ToMatrix();
}

} // namespace Grab
