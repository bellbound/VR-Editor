#include "FrameCallbackDispatcher.h"
#include "EditModeManager.h"
#include "log.h"
#include <algorithm>

FrameCallbackDispatcher* FrameCallbackDispatcher::GetSingleton()
{
    static FrameCallbackDispatcher instance;
    return &instance;
}

bool FrameCallbackDispatcher::InstallHook()
{
    // Hook into main game loop - REL::RelocationID(35565, 36564) is the main update function
    // This runs every frame on the main thread, making it safe for game object modifications.

    SKSE::AllocTrampoline(1 << 4);  // 16 bytes
    auto& trampoline = SKSE::GetTrampoline();

    REL::Relocation<std::uintptr_t> mainLoopFunc{REL::RelocationID(35565, 36564)};

    // The offset varies by game version - use REL::Relocate for cross-version compatibility
    // SE: 0x748, AE: 0xc26, VR: 0x7ee
    auto hookOffset = REL::Relocate(0x748, 0xc26, 0x7ee);

    s_originalFunc = trampoline.write_call<5>(
        mainLoopFunc.address() + hookOffset,
        &FrameCallbackDispatcher::OnMainThreadUpdate
    );

    spdlog::info("FrameCallbackDispatcher: Installed main thread hook at {:x} + 0x{:x}",
        mainLoopFunc.address(), hookOffset);

    return true;
}

void FrameCallbackDispatcher::Initialize()
{
    if (m_initialized) {
        spdlog::warn("FrameCallbackDispatcher already initialized");
        return;
    }

    m_hasLastUpdateTime = false;
    m_initialized = true;
    spdlog::info("FrameCallbackDispatcher initialized");
}

void FrameCallbackDispatcher::Shutdown()
{
    UnregisterAll();
    m_initialized = false;
    spdlog::info("FrameCallbackDispatcher shutdown");
}

void FrameCallbackDispatcher::OnMainThreadUpdate()
{
    // Call original function first
    s_originalFunc();

    auto* instance = GetSingleton();

    // Skip if not initialized
    if (!instance->m_initialized) {
        return;
    }

    // Calculate delta time
    auto now = std::chrono::steady_clock::now();
    float deltaTime = 0.016f;  // Default ~60fps

    if (instance->m_hasLastUpdateTime) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - instance->m_lastUpdateTime);
        deltaTime = elapsed.count() / 1000000.0f;
    }

    instance->m_lastUpdateTime = now;
    instance->m_hasLastUpdateTime = true;

    instance->Update(deltaTime);
}

void FrameCallbackDispatcher::Update(float deltaTime)
{
    bool inEditMode = IsInEditMode();

    // Make a copy before iterating (callbacks may modify the list)
    auto listenersCopy = m_listeners;

    for (const auto& entry : listenersCopy) {
        if (!entry.listener) continue;

        // Skip edit-mode-only listeners when not in edit mode
        if (entry.onlyInEditMode && !inEditMode) {
            continue;
        }

        entry.listener->OnFrameUpdate(deltaTime);
    }
}

void FrameCallbackDispatcher::Register(IFrameUpdateListener* listener, bool onlyInEditMode)
{
    if (!listener) {
        spdlog::warn("FrameCallbackDispatcher::Register called with null listener");
        return;
    }

    // Check if already registered
    auto it = std::find_if(m_listeners.begin(), m_listeners.end(),
        [listener](const ListenerEntry& entry) { return entry.listener == listener; });

    if (it != m_listeners.end()) {
        spdlog::warn("FrameCallbackDispatcher::Register - listener already registered");
        return;
    }

    m_listeners.push_back({listener, onlyInEditMode});
    spdlog::info("FrameCallbackDispatcher: Registered listener (onlyInEditMode={}), total: {}",
        onlyInEditMode, m_listeners.size());
}

void FrameCallbackDispatcher::Unregister(IFrameUpdateListener* listener)
{
    auto it = std::find_if(m_listeners.begin(), m_listeners.end(),
        [listener](const ListenerEntry& entry) { return entry.listener == listener; });

    if (it != m_listeners.end()) {
        m_listeners.erase(it);
        spdlog::info("FrameCallbackDispatcher: Unregistered listener, remaining: {}", m_listeners.size());
    }
}

void FrameCallbackDispatcher::UnregisterAll()
{
    m_listeners.clear();
    spdlog::info("FrameCallbackDispatcher: Unregistered all listeners");
}
