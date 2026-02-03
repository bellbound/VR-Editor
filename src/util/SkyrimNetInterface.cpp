#include "SkyrimNetInterface.h"
#include "../log.h"

SkyrimNetInterface* SkyrimNetInterface::GetSingleton()
{
    static SkyrimNetInterface instance;
    return &instance;
}

RE::BSScript::Internal::VirtualMachine* SkyrimNetInterface::GetVM()
{
    return RE::BSScript::Internal::VirtualMachine::GetSingleton();
}

void SkyrimNetInterface::Initialize()
{
    if (m_initialized) {
        return;
    }

    spdlog::info("SkyrimNetInterface: Initializing...");

    m_available = CheckAvailability();

    if (!m_available) {
        spdlog::info("SkyrimNetInterface: SkyrimNet not detected (optional integration)");
        m_initialized = true;
        return;
    }

    m_hasHotkeyFunctions = CheckHotkeyFunctions();

    if (m_hasHotkeyFunctions) {
        spdlog::info("SkyrimNetInterface: Hotkey control functions available");
    } else {
        spdlog::info("SkyrimNetInterface: Hotkey control functions not found (older SkyrimNet version?)");
    }

    m_initialized = true;
    spdlog::info("SkyrimNetInterface: Initialized successfully, SkyrimNet is available");
}

bool SkyrimNetInterface::CheckAvailability()
{
    auto* vm = GetVM();
    if (!vm) {
        return false;
    }

    RE::BSFixedString scriptName(API_SCRIPT);
    RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;

    if (!vm->GetScriptObjectType1(scriptName, typeInfo) || !typeInfo) {
        return false;
    }

    spdlog::debug("SkyrimNetInterface: Found SkyrimNetApi script");
    return true;
}

bool SkyrimNetInterface::CheckHotkeyFunctions()
{
    auto* vm = GetVM();
    if (!vm) {
        return false;
    }

    RE::BSFixedString scriptName(API_SCRIPT);
    RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;

    if (!vm->GetScriptObjectType1(scriptName, typeInfo) || !typeInfo) {
        return false;
    }

    // Check if SetCppHotkeysEnabled function exists
    RE::BSFixedString setFuncName("SetCppHotkeysEnabled");
    auto setFunc = typeInfo->GetMemberFuncIter();
    bool hasSetFunc = false;
    bool hasGetFunc = false;

    // Iterate through functions to find our target functions
    for (uint32_t i = 0; i < typeInfo->GetNumMemberFuncs(); ++i) {
        auto& func = typeInfo->GetMemberFuncIter()[i];
        if (func.func) {
            RE::BSFixedString funcName = func.func->GetName();
            if (funcName == "SetCppHotkeysEnabled") {
                hasSetFunc = true;
                spdlog::debug("SkyrimNetInterface: Found SetCppHotkeysEnabled");
            } else if (funcName == "IsCppHotkeysEnabled") {
                hasGetFunc = true;
                spdlog::debug("SkyrimNetInterface: Found IsCppHotkeysEnabled");
            }
        }
    }

    // Also check global functions (static functions)
    for (uint32_t i = 0; i < typeInfo->GetNumGlobalFuncs(); ++i) {
        auto& func = typeInfo->GetGlobalFuncIter()[i];
        if (func.func) {
            RE::BSFixedString funcName = func.func->GetName();
            if (funcName == "SetCppHotkeysEnabled") {
                hasSetFunc = true;
                spdlog::debug("SkyrimNetInterface: Found SetCppHotkeysEnabled (global)");
            } else if (funcName == "IsCppHotkeysEnabled") {
                hasGetFunc = true;
                spdlog::debug("SkyrimNetInterface: Found IsCppHotkeysEnabled (global)");
            }
        }
    }

    return hasSetFunc && hasGetFunc;
}

int SkyrimNetInterface::SetCppHotkeysEnabled(bool enabled)
{
    if (!m_available || !m_hasHotkeyFunctions) {
        spdlog::debug("SkyrimNetInterface: SetCppHotkeysEnabled called but not available");
        return 1;  // Failure
    }

    auto* vm = GetVM();
    if (!vm) {
        spdlog::error("SkyrimNetInterface: Failed to get VM");
        return 1;
    }

    // Create callback to receive the result
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;

    // Create arguments with the boolean value
    // Note: Must be a local copy, not a reference, for MakeFunctionArguments
    bool enabledValue = enabled;
    auto args = RE::MakeFunctionArguments(std::move(enabledValue));

    // Dispatch the static call
    vm->DispatchStaticCall(API_SCRIPT, "SetCppHotkeysEnabled", args, callback);

    spdlog::info("SkyrimNetInterface: SetCppHotkeysEnabled({})", enabled);
    return 0;  // Success (fire-and-forget, actual result comes async)
}

std::optional<bool> SkyrimNetInterface::IsCppHotkeysEnabled()
{
    if (!m_available || !m_hasHotkeyFunctions) {
        spdlog::debug("SkyrimNetInterface: IsCppHotkeysEnabled called but not available");
        return std::nullopt;
    }

    auto* vm = GetVM();
    if (!vm) {
        spdlog::error("SkyrimNetInterface: Failed to get VM");
        return std::nullopt;
    }

    // Note: This is fire-and-forget since Papyrus calls are async
    // For now, we can't easily get the return value synchronously
    // The caller should track state themselves
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
    auto args = RE::MakeFunctionArguments();

    vm->DispatchStaticCall(API_SCRIPT, "IsCppHotkeysEnabled", args, callback);

    spdlog::debug("SkyrimNetInterface: IsCppHotkeysEnabled called (async, result not captured)");
    return std::nullopt;  // Can't get synchronous result from Papyrus
}
