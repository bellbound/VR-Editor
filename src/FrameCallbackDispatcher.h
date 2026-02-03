#pragma once

#include "IFrameUpdateListener.h"
#include <vector>
#include <chrono>

// Dispatches per-frame updates to registered listeners
// Hooks into the game's main thread update loop
class FrameCallbackDispatcher
{
public:
    static FrameCallbackDispatcher* GetSingleton();

    // Install the main thread hook - call once during plugin init
    static bool InstallHook();

    // Initialize the dispatcher (call after hook is installed)
    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // Register a listener for frame updates
    // onlyInEditMode: if true (default), callback only fires when in edit mode
    void Register(IFrameUpdateListener* listener, bool onlyInEditMode = true);

    // Unregister a listener
    void Unregister(IFrameUpdateListener* listener);

    // Unregister all listeners
    void UnregisterAll();

    size_t GetRegisteredCount() const { return m_listeners.size(); }

private:
    FrameCallbackDispatcher() = default;
    ~FrameCallbackDispatcher() = default;
    FrameCallbackDispatcher(const FrameCallbackDispatcher&) = delete;
    FrameCallbackDispatcher& operator=(const FrameCallbackDispatcher&) = delete;

    // Called every frame via main thread hook
    void Update(float deltaTime);

    // Main thread hook callback
    static void OnMainThreadUpdate();

    // Original function pointer (stored by trampoline)
    static inline REL::Relocation<decltype(OnMainThreadUpdate)> s_originalFunc;

    struct ListenerEntry {
        IFrameUpdateListener* listener;
        bool onlyInEditMode;
    };

    std::vector<ListenerEntry> m_listeners;
    std::chrono::steady_clock::time_point m_lastUpdateTime;
    bool m_hasLastUpdateTime = false;
    bool m_initialized = false;
};
