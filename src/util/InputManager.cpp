#include "InputManager.h"
#include "MenuChecker.h"
#include "../log.h"
#include "../interfaces/ThreeDUIInterface001.h"
#include <algorithm>
#include <cmath>
#include <string>

uint64_t InputManager::s_lastButtonState[2] = {0, 0};
uint64_t InputManager::s_blockedHeldButtons[2] = {0, 0};
uint32_t InputManager::s_blockedAxes[2] = {0, 0};
uint64_t InputManager::s_p3duiSkippedButtons[2] = {0, 0};  // Buttons skipped due to 3DUI hover

InputManager* InputManager::GetSingleton()
{
	static InputManager instance;
	return &instance;
}

void InputManager::Initialize()
{
	if (m_initialized) {
		spdlog::warn("InputManager already initialized");
		return;
	}

	m_hookManager = RequestOpenVRHookManagerObject();
	if (!m_hookManager) {
		spdlog::error("Failed to get OpenVRHookManagerAPI - is SkyrimVRTools installed?");
		m_skyrimVRToolsMissing = true;
		return;
	}

	m_vrSystem = m_hookManager->GetVRSystem();
	if (!m_vrSystem) {
		spdlog::error("Failed to get IVRSystem from hook manager");
		return;
	}

	m_hookManager->RegisterControllerStateCB(&InputManager::OnControllerStateChanged);
	m_initialized = true;

	spdlog::info("InputManager initialized with raw OpenVR hook API");
}

void InputManager::Shutdown()
{
	if (!m_initialized) {
		return;
	}

	if (m_hookManager) {
		m_hookManager->UnregisterControllerStateCB(&InputManager::OnControllerStateChanged);
		m_hookManager = nullptr;
	}

	m_buttonCallbacks.clear();
	m_axisCallbacks.clear();
	m_vrSystem = nullptr;
	m_initialized = false;
	s_blockedHeldButtons[0] = 0;
	s_blockedHeldButtons[1] = 0;
	s_blockedAxes[0] = 0;
	s_blockedAxes[1] = 0;
	spdlog::info("InputManager shut down");
}

InputManager::CallbackId InputManager::AddVrButtonCallback(uint64_t buttonMask, VrButtonCallback callback, int priority)
{
	CallbackId id = m_nextCallbackId++;

	// Insert in sorted order by priority (higher priority = earlier in list)
	auto it = std::find_if(m_buttonCallbacks.begin(), m_buttonCallbacks.end(),
		[priority](const ButtonCallbackEntry& entry) { return entry.priority < priority; });
	m_buttonCallbacks.insert(it, {id, buttonMask, std::move(callback), priority});

	spdlog::info("Added VR button callback {} for mask 0x{:X} with priority {}", id, buttonMask, priority);
	return id;
}

void InputManager::RemoveVrButtonCallback(CallbackId id)
{
	if (id == InvalidCallbackId) {
		return;
	}

	auto it = std::find_if(m_buttonCallbacks.begin(), m_buttonCallbacks.end(),
		[id](const ButtonCallbackEntry& entry) { return entry.id == id; });

	if (it != m_buttonCallbacks.end()) {
		spdlog::info("Removed VR button callback {} for mask 0x{:X}", id, it->buttonMask);
		m_buttonCallbacks.erase(it);
	}
}

InputManager::CallbackId InputManager::AddVrAxisCallback(uint32_t axisIndex, VrAxisCallback callback, int priority)
{
	CallbackId id = m_nextCallbackId++;

	// Insert in sorted order by priority (higher priority = earlier in list)
	auto it = std::find_if(m_axisCallbacks.begin(), m_axisCallbacks.end(),
		[priority](const AxisCallbackEntry& entry) { return entry.priority < priority; });
	m_axisCallbacks.insert(it, {id, axisIndex, std::move(callback), priority});

	spdlog::info("Added VR axis callback {} for axis {} with priority {}", id, axisIndex, priority);
	return id;
}

void InputManager::RemoveVrAxisCallback(CallbackId id)
{
	if (id == InvalidCallbackId) {
		return;
	}

	auto it = std::find_if(m_axisCallbacks.begin(), m_axisCallbacks.end(),
		[id](const AxisCallbackEntry& entry) { return entry.id == id; });

	if (it != m_axisCallbacks.end()) {
		spdlog::info("Removed VR axis callback {} for axis {}", id, it->axisIndex);
		m_axisCallbacks.erase(it);
	}
}

uint64_t InputManager::InvokeButtonCallbacks(bool isLeft, bool isReleased, uint64_t changedButtons)
{
	if (MenuChecker::IsGameStopped()) {
		return 0;
	}

	uint64_t blockedButtons = 0;

	// IMPORTANT: Make a copy of callbacks before iterating!
	// Callbacks may register new callbacks (e.g., opening a menu registers its interaction callback).
	// Modifying m_buttonCallbacks during iteration would invalidate iterators and crash.
	auto callbacksCopy = m_buttonCallbacks;

	// Iterate through each bit that changed to find individual button IDs
	for (int buttonId = 0; buttonId < 64; ++buttonId) {
		uint64_t mask = 1ULL << buttonId;
		if (changedButtons & mask) {
			// This button changed - invoke all matching callbacks
			for (const auto& entry : callbacksCopy) {
				if (entry.buttonMask & mask) {
					if (entry.callback(isLeft, isReleased, static_cast<vr::EVRButtonId>(buttonId))) {
						blockedButtons |= mask;
					}
				}
			}
		}
	}


	return blockedButtons;
}

uint32_t InputManager::InvokeAxisCallbacks(bool isLeft, const vr::VRControllerState_t* state)
{
	if (MenuChecker::IsGameStopped()) {
		return 0;
	}
	uint32_t blockedAxes = 0;

	// Make a copy to avoid iterator invalidation
	auto callbacksCopy = m_axisCallbacks;

	for (uint32_t axisIndex = 0; axisIndex < vr::k_unControllerStateAxisCount; ++axisIndex) {
		float x = state->rAxis[axisIndex].x;
		float y = state->rAxis[axisIndex].y;

		// Invoke all callbacks registered for this axis
		for (const auto& entry : callbacksCopy) {
			if (entry.axisIndex == axisIndex) {
				if (entry.callback(isLeft, axisIndex, x, y)) {
					blockedAxes |= (1u << axisIndex);
				}
			}
		}
	}

	return blockedAxes;
}

const char* InputManager::GetButtonName(uint64_t buttonMask)
{
	// Common VR controller buttons
	if (buttonMask & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) return "ApplicationMenu";
	if (buttonMask & vr::ButtonMaskFromId(vr::k_EButton_Grip)) return "Grip";
	if (buttonMask & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) return "Touchpad";
	if (buttonMask & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) return "Trigger";
	if (buttonMask & vr::ButtonMaskFromId(vr::k_EButton_A)) return "A";
	if (buttonMask & vr::ButtonMaskFromId(vr::k_EButton_System)) return "System";
	return "Unknown";
}

// Returns a string with all button names in the mask
std::string GetAllButtonNames(uint64_t buttonMask)
{
	if (buttonMask == 0) return "(none)";

	std::string result;

	struct ButtonInfo {
		vr::EVRButtonId id;
		const char* name;
	};

	static const ButtonInfo buttons[] = {
		{vr::k_EButton_System, "System"},
		{vr::k_EButton_ApplicationMenu, "Menu"},
		{vr::k_EButton_Grip, "Grip"},
		{vr::k_EButton_DPad_Left, "DPadL"},
		{vr::k_EButton_DPad_Up, "DPadU"},
		{vr::k_EButton_DPad_Right, "DPadR"},
		{vr::k_EButton_DPad_Down, "DPadD"},
		{vr::k_EButton_A, "A"},
		{vr::k_EButton_SteamVR_Touchpad, "Touchpad"},
		{vr::k_EButton_SteamVR_Trigger, "Trigger"},
	};

	for (const auto& btn : buttons) {
		if (buttonMask & vr::ButtonMaskFromId(btn.id)) {
			if (!result.empty()) result += "+";
			result += btn.name;
		}
	}

	// Check for any unknown bits
	uint64_t knownMask = 0;
	for (const auto& btn : buttons) {
		knownMask |= vr::ButtonMaskFromId(btn.id);
	}
	uint64_t unknownBits = buttonMask & ~knownMask;
	if (unknownBits) {
		if (!result.empty()) result += "+";
		result += fmt::format("Unknown(0x{:X})", unknownBits);
	}

	return result.empty() ? "(none)" : result;
}

bool InputManager::OnControllerStateChanged(
	vr::TrackedDeviceIndex_t unControllerDeviceIndex,
	const vr::VRControllerState_t* pControllerState,
	uint32_t unControllerStateSize,
	vr::VRControllerState_t* pOutputControllerState)
{
	auto* instance = GetSingleton();
	if (!instance->m_vrSystem) {
		return false;
	}

	vr::TrackedDeviceIndex_t leftController = instance->m_vrSystem->GetTrackedDeviceIndexForControllerRole(
		vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
	vr::TrackedDeviceIndex_t rightController = instance->m_vrSystem->GetTrackedDeviceIndexForControllerRole(
		vr::ETrackedControllerRole::TrackedControllerRole_RightHand);

	int handIndex = -1;
	bool isLeft = false;

	if (unControllerDeviceIndex == leftController) {
		handIndex = 0;
		isLeft = true;
	} else if (unControllerDeviceIndex == rightController) {
		handIndex = 1;
		isLeft = false;
	}

	if (handIndex < 0) {
		return false;  // Unknown device
	}

	// IMPORTANT: Read from pOutputControllerState, not pControllerState!
	// Other DLLs (like 3DUI) may have already processed and blocked buttons.
	// By reading from the output state, we see what's left after their processing.
	// This prevents us from handling buttons that 3DUI already consumed.
	uint64_t currentButtons = pOutputControllerState->ulButtonPressed;
	uint64_t lastButtons = s_lastButtonState[handIndex];

	// Detect newly pressed buttons (bits that are now 1 but were 0)
	uint64_t newlyPressed = currentButtons & ~lastButtons;
	// Detect newly released buttons (bits that were 1 but are now 0)
	uint64_t newlyReleased = lastButtons & ~currentButtons;

	// Check if 3DUI is handling this hand - skip trigger/grip so they don't interfere with menu interaction
	const uint64_t kP3duiBlockedButtons =
		vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger) |
		vr::ButtonMaskFromId(vr::k_EButton_Grip);

	bool p3duiHovering = false;
	if (auto* p3dui = P3DUI::GetInterface001()) {
		p3duiHovering = p3dui->IsHovering(isLeft, false);
	}

	// Filter out trigger/grip from processing if 3DUI is being hovered
	// Also track skipped presses so we skip their releases even if no longer hovering
	uint64_t pressedToProcess = newlyPressed;
	uint64_t releasedToProcess = newlyReleased;

	// Skip trigger/grip presses when hovering 3DUI
	if (p3duiHovering) {
		uint64_t skippedPressed = newlyPressed & kP3duiBlockedButtons;
		if (skippedPressed) {
			spdlog::info("[{}] SKIP PRESS (3DUI): {}", isLeft ? "L" : "R", GetAllButtonNames(skippedPressed));
			s_p3duiSkippedButtons[handIndex] |= skippedPressed;  // Remember for release
			s_blockedHeldButtons[handIndex] |= skippedPressed;  // Block from game output
			pressedToProcess &= ~skippedPressed;
		}
	}

	// Always skip releases for buttons that were skipped on press
	uint64_t skippedReleased = newlyReleased & s_p3duiSkippedButtons[handIndex];
	if (skippedReleased) {
		spdlog::info("[{}] SKIP RELEASE (3DUI): {}", isLeft ? "L" : "R", GetAllButtonNames(skippedReleased));
		s_p3duiSkippedButtons[handIndex] &= ~skippedReleased;  // Clear tracked buttons
		releasedToProcess &= ~skippedReleased;
	}

	if (pressedToProcess) {
		uint64_t blocked = instance->InvokeButtonCallbacks(isLeft, false, pressedToProcess);
		s_blockedHeldButtons[handIndex] |= blocked;  // Remember blocked buttons while held
	}

	if (releasedToProcess) {
		instance->InvokeButtonCallbacks(isLeft, true, releasedToProcess);
		s_blockedHeldButtons[handIndex] &= ~releasedToProcess;  // Stop blocking on release
	}

	// Invoke axis callbacks and track which axes to block
	// Use pOutputControllerState for the same reason as buttons - see other DLLs' modifications
	uint32_t axisBlocked = instance->InvokeAxisCallbacks(isLeft, pOutputControllerState);
	s_blockedAxes[handIndex] = axisBlocked;

	// Block consumed buttons from reaching the game - every frame while held
	if (s_blockedHeldButtons[handIndex] && pOutputControllerState) {
		pOutputControllerState->ulButtonPressed &= ~s_blockedHeldButtons[handIndex];
	}

	// Block consumed axes from reaching the game
	if (s_blockedAxes[handIndex] && pOutputControllerState) {
		for (uint32_t i = 0; i < vr::k_unControllerStateAxisCount; ++i) {
			if (s_blockedAxes[handIndex] & (1u << i)) {
				pOutputControllerState->rAxis[i].x = 0.0f;
				pOutputControllerState->rAxis[i].y = 0.0f;
			}
		}
	}

	s_lastButtonState[handIndex] = currentButtons;

	return s_blockedHeldButtons[handIndex] != 0 || s_blockedAxes[handIndex] != 0;
}
