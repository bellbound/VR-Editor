#include "EditModeTransitioner.h"
#include "EditModeManager.h"
#include "TutorialManager.h"
#include "FrameCallbackDispatcher.h"
#include "util/Raycast.h"
#include "util/VRNodes.h"
#include "util/MenuChecker.h"
#include "visuals/ObjectHighlighter.h"
#include "ui/SelectionMenu.h"
#include "ui/GalleryMenu.h"
#include "log.h"
#include <cmath>

EditModeTransitioner* EditModeTransitioner::GetSingleton()
{
    static EditModeTransitioner instance;
    return &instance;
}

void EditModeTransitioner::Initialize()
{
    if (m_initialized) {
        spdlog::warn("EditModeTransitioner already initialized");
        return;
    }

    // Register for trigger button presses
    auto* inputManager = InputManager::GetSingleton();
    if (inputManager->IsInitialized()) {
        m_triggerCallbackId = inputManager->AddVrButtonCallback(
            vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger),
            [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
                return this->OnTriggerPressed(isLeft, isReleased, buttonId);
            }
        );
    } else {
        spdlog::warn("EditModeTransitioner: InputManager not initialized");
    }

    m_initialized = true;
    spdlog::info("EditModeTransitioner initialized");
}

void EditModeTransitioner::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Unregister input callback
    if (m_triggerCallbackId != InputManager::InvalidCallbackId) {
        InputManager::GetSingleton()->RemoveVrButtonCallback(m_triggerCallbackId);
        m_triggerCallbackId = InputManager::InvalidCallbackId;
    }

    m_initialized = false;
    spdlog::info("EditModeTransitioner shutdown");
}

void EditModeTransitioner::OnFrameUpdate(float deltaTime)
{
    // Currently no per-frame logic needed
    // The detection happens on trigger press
}

bool EditModeTransitioner::IsHandInsideObject(bool isLeft)
{
    // Get HMD and hand positions
    auto* hmd = VRNodes::GetHMD();
    auto* hand = isLeft ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();

    if (!hmd || !hand) {
        return false;
    }

    RE::NiPoint3 hmdPos = hmd->world.translate;
    RE::NiPoint3 handPos = hand->world.translate;

    // Calculate direction and distance from HMD to hand
    RE::NiPoint3 hmdToHand = handPos - hmdPos;
    float distance = std::sqrt(hmdToHand.x * hmdToHand.x +
                               hmdToHand.y * hmdToHand.y +
                               hmdToHand.z * hmdToHand.z);

    if (distance <= 0.0f) {
        return false;
    }

    RE::NiPoint3 direction = {
        hmdToHand.x / distance,
        hmdToHand.y / distance,
        hmdToHand.z / distance
    };

    // Step 1: Cast ray from HMD toward hand
    // If we hit something before reaching the hand, we suspect the hand is inside an object
    RaycastResult forwardResult = Raycast::CastRay(hmdPos, direction, distance);

    if (!forwardResult.hit) {
        // No hit on forward ray - hand is not inside anything
        return false;
    }

    // We hit something before reaching the hand - hand is inside object
    spdlog::trace("EditModeTransitioner: Forward ray hit at distance {} (hand distance {})",
        forwardResult.distance, distance);
    return true;
}

bool EditModeTransitioner::OnTriggerPressed(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    // Only care about trigger press, not release
    if (isReleased) {
        return false;
    }

    // Ignore trigger presses when a blocking menu is open
    if (MenuChecker::IsGameStopped()) {
        spdlog::trace("EditModeTransitioner: Ignoring trigger press - blocking menu is open");
        m_hasLastTrigger = false;  // Reset double-tap state
        return false;
    }

    // Check if hand is inside an object
    bool insideObject = IsHandInsideObject(isLeft);

    if (!insideObject) {
        // Reset double-tap state if hand isn't inside object
        m_hasLastTrigger = false;
        return false;
    }

    // Hand is inside object - check for double-tap
    auto now = std::chrono::steady_clock::now();

    if (m_hasLastTrigger && m_lastTriggerIsLeft == isLeft && m_lastTriggerWasInsideObject) {
        // Check if within double-tap threshold
        float elapsed = std::chrono::duration<float>(now - m_lastTriggerTime).count();

        if (elapsed < m_doubleTapThreshold) {
            // Double-tap detected!
            spdlog::info("EditModeTransitioner: Double-tap detected on {} hand while inside object!",
                isLeft ? "left" : "right");

            // Toggle edit mode
            auto* editModeManager = EditModeManager::GetSingleton();
            auto* tutorialManager = TutorialManager::GetSingleton();

            if (editModeManager->IsInEditMode()) {
                spdlog::info("EditModeTransitioner: Exiting edit mode");

                // Unhighlight all objects when exiting edit mode
                ObjectHighlighter::UnhighlightAll();

                // Close the selection menu (this also handles the exit)
                SelectionMenu::GetSingleton()->OnEditModeExit();

                // Close the gallery menu if open
                GalleryMenu::GetSingleton()->OnEditModeExit();

                editModeManager->Exit();
            } else {
                // Check if quick edit entry is enabled
                if (!tutorialManager->IsQuickEditEnabled()) {
                    spdlog::info("EditModeTransitioner: Quick edit disabled, ignoring");
                    m_hasLastTrigger = false;
                    return true;  // Consume input but don't enter edit mode
                }

                spdlog::info("EditModeTransitioner: Entering edit mode via grab-grab");

                editModeManager->Enter();

                // Open the selection menu
                SelectionMenu::GetSingleton()->OnEditModeEnter();

                // Show tutorial on first use (this handles showing intro message
                // and all subsequent tutorial flow via message boxes)
                if (!tutorialManager->OnGrabGrabEditModeEnter()) {
                    // Tutorial already shown, just show the normal notification
                    RE::DebugNotification("Entered Edit mode, hold B to show menu");
                }
                // If tutorial was shown, it handles all messaging via message boxes
            }

            // Reset state after handling
            m_hasLastTrigger = false;

            // Consume the input
            return true;
        }
    }

    // Update state for next detection
    m_lastTriggerTime = now;
    m_lastTriggerIsLeft = isLeft;
    m_lastTriggerWasInsideObject = insideObject;
    m_hasLastTrigger = true;

    // Don't consume first tap
    return false;
}
