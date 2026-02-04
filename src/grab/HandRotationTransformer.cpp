#include "HandRotationTransformer.h"
#include "../util/VRNodes.h"
#include "../util/PositioningUtil.h"
#include "../log.h"
#include <cmath>

namespace Grab {

void HandRotationTransformer::Start(bool forLeftHand)
{
    m_forLeftHand = forLeftHand;
    m_startHandRotation = GetHandRotation();
    m_rawRotationDelta = RE::NiMatrix3();  // Identity
    m_smoothedRotation = RE::NiMatrix3();  // Identity
    m_isActive = true;

    spdlog::trace("HandRotationTransformer: Started for {} hand", forLeftHand ? "left" : "right");
}

void HandRotationTransformer::Update(float deltaTime)
{
    if (!m_isActive) {
        return;
    }

    // Get current hand rotation
    RE::NiMatrix3 currentHandRotation = GetHandRotation();

    // Calculate delta in the controller's LOCAL frame (not world space).
    // localDelta = startHand⁻¹ × currentHand
    // This ensures the controller's yaw/pitch/roll map to the object's local axes
    // regardless of the object's current orientation. A world-space delta (current × start⁻¹)
    // would cause controller pitch to become object roll (etc.) when the object is already rotated.
    RE::NiMatrix3 startInverse = InvertRotation(m_startHandRotation);
    RE::NiMatrix3 localDelta = MultiplyRotations(startInverse, currentHandRotation);

    // INVERT the rotation: we want the object to rotate opposite to hand movement
    // This makes the control feel more intuitive (like steering a wheel)
    m_rawRotationDelta = InvertRotation(localDelta);

    // Apply rotation directly without smoothing (smoothing disabled)
    m_smoothedRotation = m_rawRotationDelta;
}

void HandRotationTransformer::Finish()
{
    if (!m_isActive) {
        return;
    }

    // "Bake in" the current smoothed rotation into the accumulated rotation
    // This prevents the rotation from snapping back when we release the trigger
    // Right-multiply: local-frame deltas accumulate in application order (acc × new)
    m_accumulatedRotation = MultiplyRotations(m_accumulatedRotation, m_smoothedRotation);

    // Reset for next gesture
    m_rawRotationDelta = RE::NiMatrix3();
    m_smoothedRotation = RE::NiMatrix3();
    m_isActive = false;

    spdlog::trace("HandRotationTransformer: Finished, rotation baked into accumulated");
}

RE::NiPoint3 HandRotationTransformer::GetEulerDelta() const
{
    return PositioningUtil::MatrixToEulerAngles(m_smoothedRotation);
}

RE::NiPoint3 HandRotationTransformer::GetAccumulatedEulerDelta() const
{
    return PositioningUtil::MatrixToEulerAngles(m_accumulatedRotation);
}

void HandRotationTransformer::Reset()
{
    m_isActive = false;
    m_startHandRotation = RE::NiMatrix3();
    m_rawRotationDelta = RE::NiMatrix3();
    m_smoothedRotation = RE::NiMatrix3();
    m_accumulatedRotation = RE::NiMatrix3();
}

RE::NiMatrix3 HandRotationTransformer::GetHandRotation() const
{
    RE::NiAVObject* hand = m_forLeftHand ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();
    if (hand) {
        return hand->world.rotate;
    }
    return RE::NiMatrix3();  // Identity
}

RE::NiMatrix3 HandRotationTransformer::InvertRotation(const RE::NiMatrix3& m)
{
    // For rotation matrices, the inverse is the transpose
    RE::NiMatrix3 result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result.entry[i][j] = m.entry[j][i];
        }
    }
    return result;
}

RE::NiMatrix3 HandRotationTransformer::MultiplyRotations(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
{
    return PositioningUtil::MultiplyMatrices(a, b);
}

} // namespace Grab
