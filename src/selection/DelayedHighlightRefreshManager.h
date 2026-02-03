#pragma once

#include "../IFrameUpdateListener.h"
#include <RE/Skyrim.h>
#include <vector>

namespace Selection {

// DelayedHighlightRefreshManager: Delays highlight re-application after Disable/Enable cycles
//
// Problem: When objects are disabled and re-enabled (to reset collision), the 3D scene graph
// is destroyed and recreated. Immediately reapplying highlights can fail because the new
// 3D node may not be fully initialized yet.
//
// Solution: Wait a configurable delay (default 2.0 seconds) after the Disable/Enable cycle
// before reapplying the selection highlight. This gives the engine time to fully reconstruct
// the 3D representation.
//
// Usage:
// - Instead of calling SelectionState::RefreshHighlightIfSelected() immediately,
//   call DelayedHighlightRefreshManager::ScheduleRefresh(ref)
// - The manager will track the object and reapply the highlight after the delay expires
//
class DelayedHighlightRefreshManager : public IFrameUpdateListener
{
public:
    static DelayedHighlightRefreshManager* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Schedule a delayed highlight refresh for an object
    // If the object is already scheduled, the timer is reset
    void ScheduleRefresh(RE::TESObjectREFR* ref);

    // Schedule refresh using FormID (useful when ref may become invalid)
    void ScheduleRefresh(RE::FormID formId);

    // Cancel any pending refresh for an object
    void CancelRefresh(RE::TESObjectREFR* ref);
    void CancelRefresh(RE::FormID formId);

    // Clear all pending refreshes
    void ClearAll();

    // Check if an object has a pending refresh
    bool IsPending(RE::FormID formId) const;

    // Configuration
    static constexpr float kDefaultDelaySeconds = 0.2f;  // Default delay before refresh (200ms)

private:
    DelayedHighlightRefreshManager() = default;
    ~DelayedHighlightRefreshManager() = default;
    DelayedHighlightRefreshManager(const DelayedHighlightRefreshManager&) = delete;
    DelayedHighlightRefreshManager& operator=(const DelayedHighlightRefreshManager&) = delete;

    // Pending refresh entry
    struct PendingRefresh {
        RE::FormID formId = 0;
        float remainingTime = 0.0f;
    };

    // Perform the actual highlight refresh
    void PerformRefresh(RE::FormID formId);

    std::vector<PendingRefresh> m_pendingRefreshes;
    bool m_initialized = false;
};

} // namespace Selection
