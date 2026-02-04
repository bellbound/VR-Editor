#pragma once

#include <vector>

namespace RE {
    class TESObjectREFR;
}

namespace SelectionLogger {
    // Logs the set of unique form types in the selection, plus individual
    // formID/baseID pairs if the count is under 100. Useful for diagnosing
    // crashes caused by moving unexpected object types via sphere selection.
    void LogSelectedObjects(const std::vector<RE::TESObjectREFR*>& objects);
}
