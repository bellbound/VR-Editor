#include "HandTranslationTransformer.h"
#include "../util/VRNodes.h"
#include "../log.h"
#include <cmath>

namespace Grab {

void HandTranslationTransformer::Start(bool forLeftHand)
{
    m_forLeftHand = forLeftHand;
    m_startHandPosition = GetHandPosition();
    m_rawTranslationDelta = RE::NiPoint3{0, 0, 0};
    m_smoothedTranslation = RE::NiPoint3{0, 0, 0};
    m_isActive = true;

    spdlog::trace("HandTranslationTransformer: Started for {} hand at ({:.1f}, {:.1f}, {:.1f})",
        forLeftHand ? "left" : "right",
        m_startHandPosition.x, m_startHandPosition.y, m_startHandPosition.z);
}

void HandTranslationTransformer::Update(float deltaTime)
{
    if (!m_isActive) {
        return;
    }

    // Get current hand position
    RE::NiPoint3 currentHandPosition = GetHandPosition();

    // Calculate delta: how much the hand has moved since Start()
    m_rawTranslationDelta = RE::NiPoint3{
        (currentHandPosition.x - m_startHandPosition.x) * TRANSLATION_SCALE,
        (currentHandPosition.y - m_startHandPosition.y) * TRANSLATION_SCALE,
        (currentHandPosition.z - m_startHandPosition.z) * TRANSLATION_SCALE
    };

    // Smooth the translation using exponential interpolation
    if (deltaTime > 0.0f && deltaTime < 0.1f) {
        float t = 1.0f - std::exp(-m_smoothingSpeed * deltaTime);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        m_smoothedTranslation = TransformSmoother::LerpPosition(m_smoothedTranslation, m_rawTranslationDelta, t);
    }
}

void HandTranslationTransformer::Finish()
{
    if (!m_isActive) {
        return;
    }

    // "Bake in" the current smoothed translation into the accumulated translation
    // This prevents the translation from snapping back when we release the trigger
    m_accumulatedTranslation.x += m_smoothedTranslation.x;
    m_accumulatedTranslation.y += m_smoothedTranslation.y;
    m_accumulatedTranslation.z += m_smoothedTranslation.z;

    // Reset for next gesture
    m_rawTranslationDelta = RE::NiPoint3{0, 0, 0};
    m_smoothedTranslation = RE::NiPoint3{0, 0, 0};
    m_isActive = false;

    spdlog::trace("HandTranslationTransformer: Finished, translation baked into accumulated ({:.1f}, {:.1f}, {:.1f})",
        m_accumulatedTranslation.x, m_accumulatedTranslation.y, m_accumulatedTranslation.z);
}

void HandTranslationTransformer::Reset()
{
    m_isActive = false;
    m_startHandPosition = RE::NiPoint3{0, 0, 0};
    m_rawTranslationDelta = RE::NiPoint3{0, 0, 0};
    m_smoothedTranslation = RE::NiPoint3{0, 0, 0};
    m_accumulatedTranslation = RE::NiPoint3{0, 0, 0};
}

RE::NiPoint3 HandTranslationTransformer::GetHandPosition() const
{
    RE::NiAVObject* hand = m_forLeftHand ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();
    if (hand) {
        return hand->world.translate;
    }
    return RE::NiPoint3{0, 0, 0};
}

} // namespace Grab
