#pragma once

#include <RE/Skyrim.h>
#include <string>

namespace Util::ActionLogger {

    // ========== Formatting Helpers (return strings, don't log) ==========

    // Get a display name for a form: tries editor ID, then base object editor ID, then hex FormID
    std::string GetDisplayName(RE::FormID formId);

    // Format a complete transform state: "pos=(x, y, z) rot=(x, y, z) scale=s"
    // Euler angles are converted from radians to degrees for readability
    std::string FormatTransform(const RE::NiPoint3& pos, const RE::NiPoint3& eulerRad, float scale);

    // Format a position delta with +/- signs: "delta=(+dx, +dy, +dz)"
    std::string FormatPositionDelta(const RE::NiPoint3& before, const RE::NiPoint3& after);

    // ========== Logging Helpers (format and print via spdlog::info) ==========

    // Log a section header: "=== context (N objects) ==="
    void LogHeader(const char* context, size_t objectCount);

    // Log a single object's transform snapshot (e.g., grab start)
    //   [index/total] "EditorID" (FormID): pos=(...) rot=(...) scale=...
    void LogSnapshot(int index, int total, RE::FormID formId,
                     const RE::NiPoint3& pos, const RE::NiPoint3& eulerRad, float scale);

    // Log a single object's before/after transform change (e.g., grab end, undo, redo)
    //   [index/total] "EditorID" (FormID):
    //     Before: pos=(...) rot=(...) scale=...
    //     After:  pos=(...) rot=(...) scale=...
    void LogChange(int index, int total, RE::FormID formId,
                   const RE::NiPoint3& beforePos, const RE::NiPoint3& beforeEuler, float beforeScale,
                   const RE::NiPoint3& afterPos, const RE::NiPoint3& afterEuler, float afterScale);

} // namespace Util::ActionLogger
