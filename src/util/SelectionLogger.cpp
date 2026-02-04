#include "SelectionLogger.h"
#include "../log.h"

#include <set>
#include <string>

namespace SelectionLogger {

void LogSelectedObjects(const std::vector<RE::TESObjectREFR*>& objects)
{
    if (objects.empty()) {
        return;
    }

    // Collect unique form types from base objects
    std::set<RE::FormType> formTypes;
    for (auto* ref : objects) {
        if (!ref) continue;
        auto* baseObj = ref->GetBaseObject();
        if (baseObj) {
            formTypes.insert(baseObj->GetFormType());
        }
    }

    // Build a comma-separated list of form type names
    std::string typeList;
    for (auto ft : formTypes) {
        if (!typeList.empty()) {
            typeList += ", ";
        }
        typeList += RE::FormTypeToString(ft);
    }

    spdlog::info("SphereSelection: {} objects selected, {} unique form types: [{}]",
        objects.size(), formTypes.size(), typeList);

    // Log individual formID / baseID pairs if under 100 objects
    if (objects.size() < 100) {
        for (auto* ref : objects) {
            if (!ref) continue;
            auto* baseObj = ref->GetBaseObject();
            spdlog::info("  ref {:08X} base {:08X} ({})",
                ref->GetFormID(),
                baseObj ? baseObj->GetFormID() : 0,
                baseObj ? RE::FormTypeToString(baseObj->GetFormType()) : "null");
        }
    } else {
        spdlog::info("SphereSelection: Skipping individual ref logging (count {} >= 100)", objects.size());
    }
}

} // namespace SelectionLogger
