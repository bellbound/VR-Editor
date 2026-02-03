#include "TutorialManager.h"
#include "EditModeManager.h"
#include "config/ConfigStorage.h"
#include "config/ConfigOptions.h"
#include "util/MessageBoxUtil.h"
#include "visuals/ObjectHighlighter.h"
#include "ui/SelectionMenu.h"
#include "ui/GalleryMenu.h"
#include "log.h"

TutorialManager* TutorialManager::GetSingleton()
{
    static TutorialManager instance;
    return &instance;
}

bool TutorialManager::HasShownTutorial() const
{
    return Config::ConfigStorage::GetSingleton()->GetInt(Config::Options::kTutorialShown, 0) != 0;
}

bool TutorialManager::IsQuickEditEnabled() const
{
    auto* config = Config::ConfigStorage::GetSingleton();
    int value = config->GetInt(Config::Options::kQuickEditEnabled, 1);
    spdlog::info("TutorialManager::IsQuickEditEnabled: Read value {} from '{}', returning {}", 
        value, config->GetIniPath(), value != 0);
    return value != 0;
}

bool TutorialManager::OnGrabGrabEditModeEnter()
{
    // If quick edit is disabled, don't allow entry (caller should check first)
    if (!IsQuickEditEnabled()) {
        spdlog::info("TutorialManager: Quick edit is disabled, not showing tutorial");
        return false;
    }

    // If tutorial already shown, don't show again
    if (HasShownTutorial()) {
        spdlog::trace("TutorialManager: Tutorial already shown, skipping");
        return false;
    }

    spdlog::info("TutorialManager: Showing first-time tutorial");
    ShowIntroMessage();
    return true;
}

void TutorialManager::SetQuickEditEnabled(bool enabled)
{
    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kQuickEditEnabled, enabled ? 1 : 0);
    spdlog::info("TutorialManager: Quick edit mode {}", enabled ? "enabled" : "disabled");
}

void TutorialManager::ResetTutorialState()
{
    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kTutorialShown, 0);
    spdlog::info("TutorialManager: Tutorial state reset");
}

void TutorialManager::ShowIntroMessage()
{
    MessageBoxUtil::Show(
        "VR Positioner: You entered edit mode by inserting your hand into a static object "
        "and pressing Trigger twice. You can exit Edit mode the same way. "
        "How do you want to continue?",
        {
            "Exit edit mode",
            "Let me edit skyrim's static objects!",
            "How does this work?",
            "Exit edit mode & disable entering edit mode this way"
        },
        [this](unsigned int buttonIndex) {
            switch (buttonIndex) {
                case 0:  // Exit edit mode
                    spdlog::info("TutorialManager: User chose to exit edit mode");
                    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kTutorialShown, 1);
                    ExitEditMode();
                    break;

                case 1:  // Let me edit!
                    spdlog::info("TutorialManager: User chose to start editing");
                    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kTutorialShown, 1);
                    ShowQuickTips();
                    break;

                case 2:  // How does this work?
                    spdlog::info("TutorialManager: User chose tutorial");
                    ShowTutorialStep(1);
                    break;

                case 3:  // Exit & disable
                    spdlog::info("TutorialManager: User chose to disable quick edit");
                    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kTutorialShown, 1);
                    SetQuickEditEnabled(false);
                    ExitEditMode();
                    ShowQuickEditDisabledMessage();
                    break;
            }
        }
    );
}

void TutorialManager::ShowTutorialStep(int step)
{
    std::string message;

    switch (step) {
        case 1:
            message = "[1/5] Your hand is now a laser pointer! Press and hold trigger "
                      "to move the highlighted object.";
            break;
        case 2:
            message = "[2/5] Hold the B button to show the Edit Mode wheel menu.";
            break;
        case 3:
            message = "[3/5] All your edits are saved when you save your game. "
                      "Base Object Swapper files are automatically saved to your "
                      "Data directory / overwrite folder so you can share your edits.";
            break;
        case 4:
            message = "[4/5] To exit edit mode, use the button in the wheel menu.";
            break;
        case 5:
            message = "[5/5] You can also enter or exit edit mode by inserting your "
                      "hand into any static object and pressing trigger twice.";
            break;
        default:
            // Tutorial complete
            Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kTutorialShown, 1);
            return;
    }

    // Show message with Next / Skip Tutorial buttons
    MessageBoxUtil::Show(
        message,
        {"Next", "Skip tutorial"},
        [this, step](unsigned int buttonIndex) {
            if (buttonIndex == 0) {
                // Next
                if (step < kTutorialStepCount) {
                    ShowTutorialStep(step + 1);
                } else {
                    // Tutorial complete
                    Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kTutorialShown, 1);
                    spdlog::info("TutorialManager: Tutorial completed");
                }
            } else {
                // Skip tutorial
                Config::ConfigStorage::GetSingleton()->SetInt(Config::Options::kTutorialShown, 1);
                spdlog::info("TutorialManager: Tutorial skipped");
            }
        }
    );
}

void TutorialManager::ShowQuickTips()
{
    MessageBoxUtil::ShowOK(
        "Press & Hold trigger to position the highlighted object. "
        "Hold B to bring up the Wheel Menu. Have fun editing!"
    );
}

void TutorialManager::ShowQuickEditDisabledMessage()
{
    MessageBoxUtil::ShowOK(
        "Quick Entry for Edit mode disabled. Set up a VRIK gesture to enter "
        "edit mode or enable it again in the VR Editor MCM."
    );
}

void TutorialManager::ExitEditMode()
{
    auto* editModeManager = EditModeManager::GetSingleton();
    if (editModeManager->IsInEditMode()) {
        // Unhighlight all objects when exiting edit mode
        ObjectHighlighter::UnhighlightAll();

        // Close menus
        SelectionMenu::GetSingleton()->OnEditModeExit();
        GalleryMenu::GetSingleton()->OnEditModeExit();

        editModeManager->Exit();
        spdlog::info("TutorialManager: Exited edit mode");
    }
}
