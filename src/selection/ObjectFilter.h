#pragma once

#include <RE/Skyrim.h>

namespace Selection {

// ObjectFilter: Centralized filtering for what objects can be selected/highlighted
//
// This class provides a single point to control which objects are allowed to be:
// - Highlighted during hover
// - Added to the selection
//
// The filter is checked at multiple integration points:
// - ObjectHighlighter::Highlight() - prevents visual highlight
// - SelectionState methods - prevents adding to selection
// - SphereSelectionController - filters out objects from sphere scan results
//
// Current filters:
// - Static markers (XMarker, MapMarker, etc.) - editor placeholders not meant for selection
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
    // Logs filtered objects with base ID, form ID, and reason
    static bool ShouldProcess(RE::TESObjectREFR* ref);

private:
    // Check if a Static object is a filtered marker type (editor placeholders)
    // These are invisible markers used by the Creation Kit for various purposes
    static bool IsFilteredMarker(const char* editorId);
};

} // namespace Selection
