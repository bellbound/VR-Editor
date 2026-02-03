#include "DelayedHighlightRefreshManager.h"
#include "SelectionState.h"
#include "../FrameCallbackDispatcher.h"
#include "../log.h"
#include <algorithm>

namespace Selection {

DelayedHighlightRefreshManager* DelayedHighlightRefreshManager::GetSingleton()
{
    static DelayedHighlightRefreshManager instance;
    return &instance;
}

void DelayedHighlightRefreshManager::Initialize()
{
    if (m_initialized) {
        spdlog::warn("DelayedHighlightRefreshManager already initialized");
        return;
    }

    // Register for frame callbacks (only in edit mode)
    FrameCallbackDispatcher::GetSingleton()->Register(this, true);

    m_initialized = true;
    spdlog::info("DelayedHighlightRefreshManager initialized");
}

void DelayedHighlightRefreshManager::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Clear all pending refreshes
    ClearAll();

    // Unregister from frame callbacks
    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    m_initialized = false;
    spdlog::info("DelayedHighlightRefreshManager shutdown");
}

void DelayedHighlightRefreshManager::OnFrameUpdate(float deltaTime)
{
    if (m_pendingRefreshes.empty()) {
        return;
    }

    // Process all pending refreshes
    // We iterate backwards to allow safe removal during iteration
    for (auto it = m_pendingRefreshes.begin(); it != m_pendingRefreshes.end(); ) {
        it->remainingTime -= deltaTime;

        if (it->remainingTime <= 0.0f) {
            // Timer expired - perform the refresh
            PerformRefresh(it->formId);
            it = m_pendingRefreshes.erase(it);
        } else {
            ++it;
        }
    }
}

void DelayedHighlightRefreshManager::ScheduleRefresh(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }
    ScheduleRefresh(ref->GetFormID());
}

void DelayedHighlightRefreshManager::ScheduleRefresh(RE::FormID formId)
{
    if (formId == 0) {
        return;
    }

    // Check if already scheduled - if so, reset the timer
    for (auto& pending : m_pendingRefreshes) {
        if (pending.formId == formId) {
            pending.remainingTime = kDefaultDelaySeconds;
            spdlog::trace("DelayedHighlightRefreshManager: Reset timer for {:08X} to {:.1f}s",
                formId, kDefaultDelaySeconds);
            return;
        }
    }

    // Add new entry
    PendingRefresh entry;
    entry.formId = formId;
    entry.remainingTime = kDefaultDelaySeconds;
    m_pendingRefreshes.push_back(entry);

    spdlog::info("DelayedHighlightRefreshManager: Scheduled refresh for {:08X} in {:.1f}s",
        formId, kDefaultDelaySeconds);
}

void DelayedHighlightRefreshManager::CancelRefresh(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return;
    }
    CancelRefresh(ref->GetFormID());
}

void DelayedHighlightRefreshManager::CancelRefresh(RE::FormID formId)
{
    auto it = std::find_if(m_pendingRefreshes.begin(), m_pendingRefreshes.end(),
        [formId](const PendingRefresh& p) { return p.formId == formId; });

    if (it != m_pendingRefreshes.end()) {
        spdlog::trace("DelayedHighlightRefreshManager: Cancelled refresh for {:08X}", formId);
        m_pendingRefreshes.erase(it);
    }
}

void DelayedHighlightRefreshManager::ClearAll()
{
    if (!m_pendingRefreshes.empty()) {
        spdlog::info("DelayedHighlightRefreshManager: Cleared {} pending refreshes",
            m_pendingRefreshes.size());
        m_pendingRefreshes.clear();
    }
}

bool DelayedHighlightRefreshManager::IsPending(RE::FormID formId) const
{
    return std::any_of(m_pendingRefreshes.begin(), m_pendingRefreshes.end(),
        [formId](const PendingRefresh& p) { return p.formId == formId; });
}

void DelayedHighlightRefreshManager::PerformRefresh(RE::FormID formId)
{
    // Look up the reference by FormID
    auto* form = RE::TESForm::LookupByID(formId);
    if (!form) {
        spdlog::trace("DelayedHighlightRefreshManager: Form {:08X} no longer exists, skipping refresh",
            formId);
        return;
    }

    auto* ref = form->AsReference();
    if (!ref) {
        spdlog::trace("DelayedHighlightRefreshManager: Form {:08X} is not a reference, skipping refresh",
            formId);
        return;
    }

    // Check if the object still has a valid 3D representation
    if (!ref->Get3D()) {
        spdlog::trace("DelayedHighlightRefreshManager: Ref {:08X} has no 3D, skipping refresh",
            formId);
        return;
    }

    // Delegate to SelectionState to apply the highlight if still selected
    SelectionState::GetSingleton()->RefreshHighlightIfSelected(ref);

    spdlog::info("DelayedHighlightRefreshManager: Performed delayed highlight refresh for {:08X}",
        formId);
}

} // namespace Selection
