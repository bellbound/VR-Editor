#include "ActionLogger.h"
#include "../log.h"

#include <cmath>

namespace {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
}

namespace Util::ActionLogger {

std::string GetDisplayName(RE::FormID formId)
{
    auto* form = RE::TESForm::LookupByID(formId);
    if (!form) {
        return fmt::format("{:08X}", formId);
    }

    // Try to get editor ID
    const char* editorId = form->GetFormEditorID();
    if (editorId && editorId[0] != '\0') {
        return std::string(editorId);
    }

    // Try to get the base object's editor ID if this is a reference
    if (auto* ref = form->As<RE::TESObjectREFR>()) {
        if (auto* baseObj = ref->GetBaseObject()) {
            editorId = baseObj->GetFormEditorID();
            if (editorId && editorId[0] != '\0') {
                return std::string(editorId);
            }
        }
    }

    // Fallback to hex form ID
    return fmt::format("{:08X}", formId);
}

std::string FormatTransform(const RE::NiPoint3& pos, const RE::NiPoint3& eulerRad, float scale)
{
    return fmt::format("pos=({:.1f}, {:.1f}, {:.1f}) rot=({:.1f}, {:.1f}, {:.1f}) scale={:.3f}",
        pos.x, pos.y, pos.z,
        eulerRad.x * kRadToDeg, eulerRad.y * kRadToDeg, eulerRad.z * kRadToDeg,
        scale);
}

std::string FormatPositionDelta(const RE::NiPoint3& before, const RE::NiPoint3& after)
{
    float dx = after.x - before.x;
    float dy = after.y - before.y;
    float dz = after.z - before.z;
    return fmt::format("delta=({:+.1f}, {:+.1f}, {:+.1f})", dx, dy, dz);
}

void LogHeader(const char* context, size_t objectCount)
{
    spdlog::info("=== {} ({} object{}) ===", context, objectCount, objectCount == 1 ? "" : "s");
}

void LogSnapshot(int index, int total, RE::FormID formId,
                 const RE::NiPoint3& pos, const RE::NiPoint3& eulerRad, float scale)
{
    std::string name = GetDisplayName(formId);
    std::string transform = FormatTransform(pos, eulerRad, scale);
    spdlog::info("  [{}/{}] {} ({:08X}): {}", index, total, name, formId, transform);
}

void LogChange(int index, int total, RE::FormID formId,
               const RE::NiPoint3& beforePos, const RE::NiPoint3& beforeEuler, float beforeScale,
               const RE::NiPoint3& afterPos, const RE::NiPoint3& afterEuler, float afterScale)
{
    std::string name = GetDisplayName(formId);
    std::string beforeStr = FormatTransform(beforePos, beforeEuler, beforeScale);
    std::string afterStr = FormatTransform(afterPos, afterEuler, afterScale);
    std::string delta = FormatPositionDelta(beforePos, afterPos);

    spdlog::info("  [{}/{}] {} ({:08X}):", index, total, name, formId);
    spdlog::info("    Before: {}", beforeStr);
    spdlog::info("    After:  {}", afterStr);
    spdlog::info("    {}", delta);
}

} // namespace Util::ActionLogger
