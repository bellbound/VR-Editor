#pragma once
#include "VRHookAPI.h"
#include <functional>
#include <vector>

class InputManager
{
public:
	// Return true to consume/block the input, false to let it pass through
	using VrButtonCallback = std::function<bool(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)>;

	// Axis callback: receives hand, axis index, x/y values. Return true to consume.
	using VrAxisCallback = std::function<bool(bool isLeft, uint32_t axisIndex, float x, float y)>;

	using CallbackId = uint32_t;
	static constexpr CallbackId InvalidCallbackId = 0;

	static InputManager* GetSingleton();

	void Initialize();
	void Shutdown();

	bool IsInitialized() const { return m_initialized; }
	bool IsSkyrimVRToolsMissing() const { return m_skyrimVRToolsMissing; }

	// Register a callback for specific button(s). Returns an ID for removal.
	// Higher priority callbacks are invoked first (default = 0, use 100+ for UI that should consume first)
	CallbackId AddVrButtonCallback(uint64_t buttonMask, VrButtonCallback callback, int priority = 0);

	// Remove a button callback by its ID
	void RemoveVrButtonCallback(CallbackId id);

	// Register a callback for specific axis. Returns an ID for removal.
	// axisIndex: 0=thumbstick/touchpad, 1=trigger, 2=secondary thumbstick (controller dependent)
	// Higher priority callbacks are invoked first (default = 0)
	CallbackId AddVrAxisCallback(uint32_t axisIndex, VrAxisCallback callback, int priority = 0);

	// Remove an axis callback by its ID
	void RemoveVrAxisCallback(CallbackId id);

private:
	InputManager() = default;
	~InputManager() = default;
	InputManager(const InputManager&) = delete;
	InputManager& operator=(const InputManager&) = delete;

	static bool OnControllerStateChanged(
		vr::TrackedDeviceIndex_t unControllerDeviceIndex,
		const vr::VRControllerState_t* pControllerState,
		uint32_t unControllerStateSize,
		vr::VRControllerState_t* pOutputControllerState);

	uint64_t InvokeButtonCallbacks(bool isLeft, bool isReleased, uint64_t changedButtons);
	uint32_t InvokeAxisCallbacks(bool isLeft, const vr::VRControllerState_t* state);
	static const char* GetButtonName(uint64_t buttonMask);

	struct ButtonCallbackEntry {
		CallbackId id;
		uint64_t buttonMask;
		VrButtonCallback callback;
		int priority;  // Higher = called first
	};

	struct AxisCallbackEntry {
		CallbackId id;
		uint32_t axisIndex;
		VrAxisCallback callback;
		int priority;  // Higher = called first
	};

	OpenVRHookManagerAPI* m_hookManager = nullptr;
	vr::IVRSystem* m_vrSystem = nullptr;
	bool m_initialized = false;
	bool m_skyrimVRToolsMissing = false;
	std::vector<ButtonCallbackEntry> m_buttonCallbacks;
	std::vector<AxisCallbackEntry> m_axisCallbacks;
	CallbackId m_nextCallbackId = 1;  // 0 is InvalidCallbackId

	static uint64_t s_lastButtonState[2];     // Left=0, Right=1
	static uint64_t s_blockedHeldButtons[2];  // Buttons currently blocked while held
	static uint32_t s_blockedAxes[2];         // Bitmask of blocked axes (bit 0 = axis 0, etc.)
	static uint64_t s_p3duiSkippedButtons[2]; // Buttons skipped due to 3DUI hover (skip release too)
};
