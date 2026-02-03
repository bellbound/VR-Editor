#pragma once

#include <functional>
#include "config/ConfigStorage.h"
#include "config/ConfigOptions.h"

// TutorialManager: Handles first-time user experience for VR Positioner edit mode
//
// Shows tutorial message boxes when user first enters edit mode via the "grab grab"
// gesture (inserting hand into static object and double-tapping trigger).
// Does NOT show when entering via VRIK gesture.
//
// Flow:
// 1. First "grab grab" entry triggers intro message with 4 options
// 2. "How does this work?" shows 5-step tutorial sequence
// 3. "Let me Edit!" shows quick tips
// 4. "Exit & disable" disables quick edit mode entirely
//
class TutorialManager
{
public:
    static TutorialManager* GetSingleton();

    // Call when user enters edit mode via "grab grab" gesture
    // Shows tutorial if this is the first time, otherwise does nothing
    // Returns true if tutorial was shown (caller should wait for user to finish)
    bool OnGrabGrabEditModeEnter();

    // Check if the "grab grab" quick edit entry is enabled
    // When disabled, EditModeTransitioner should not allow edit mode entry
    bool IsQuickEditEnabled() const;

    // Enable/disable quick edit mode entry (called from MCM or tutorial)
    void SetQuickEditEnabled(bool enabled);

    // Check if tutorial has been shown
    bool HasShownTutorial() const;

    // Reset tutorial state (for testing/debugging)
    void ResetTutorialState();

private:
    TutorialManager() = default;
    ~TutorialManager() = default;
    TutorialManager(const TutorialManager&) = delete;
    TutorialManager& operator=(const TutorialManager&) = delete;

    // Show the intro message box
    void ShowIntroMessage();

    // Show the tutorial sequence (called when user picks "How does this work?")
    void ShowTutorialStep(int step);

    // Show the quick tips message (called when user picks "Let me Edit!")
    void ShowQuickTips();

    // Show the "disabled" confirmation message
    void ShowQuickEditDisabledMessage();

    // Exit edit mode helper
    void ExitEditMode();

    // Total number of tutorial steps
    static constexpr int kTutorialStepCount = 5;
};
