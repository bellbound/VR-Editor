#pragma once

#include <RE/Skyrim.h>

namespace Selection {

// ObjectFilter: Centralized filtering for what objects can be selected/highlighted
//
// This class provides a single point to control which objects are allowed to be:
// - Highlighted during hover
// - Added to the selection
//
// Currently allows everything through (filters will be added in future iterations).
// The filter is checked at multiple integration points:
// - ObjectHighlighter::Highlight() - prevents visual highlight
// - SelectionState methods - prevents adding to selection
// - SphereSelectionController - filters out objects from sphere scan results
//
// Usage:
//   if (!ObjectFilter::ShouldProcess(ref)) {
//       return;  // Skip this object
//   }
//
class ObjectFilter
{
public:
    // Check if a reference should be processed for selection/highlighting
    // Returns true if the object passes all filters, false if it should be ignored
    //
    // Currently returns true for all valid references (no filters active).
    // Future filters may check:
    // - Object type (static, actor, etc.)
    // - Mod source (base game, specific plugins)
    // - Custom user-defined lists
    // - Distance or visibility constraints
    static bool ShouldProcess(RE::TESObjectREFR* ref);
};

} // namespace Selection
