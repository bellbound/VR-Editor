#include "ObjectFilter.h"

namespace Selection {

bool ObjectFilter::ShouldProcess(RE::TESObjectREFR* ref)
{
    // Basic null check - null references never pass
    if (!ref) {
        return false;
    }

    // Currently allow everything through
    // Future filters will be added here, for example:
    // - if (!ShouldProcessByFormType(ref)) return false;
    // - if (!ShouldProcessByMod(ref)) return false;
    // - if (IsInBlacklist(ref)) return false;

    return true;
}

} // namespace Selection
