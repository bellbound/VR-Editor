#include "NotificationManager.h"
#include "../log.h"

NotificationManager* NotificationManager::GetSingleton()
{
    static NotificationManager instance;
    return &instance;
}

bool NotificationManager::Show(std::string_view message)
{
    auto now = std::chrono::steady_clock::now();

    // Check if we're in cooldown
    if (m_hasShownNotification) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastNotificationTime);
        float elapsedSeconds = elapsed.count() / 1000.0f;

        if (elapsedSeconds < m_cooldownDuration) {
            spdlog::trace("NotificationManager: Swallowed '{}' (cooldown: {:.2f}s remaining)",
                message, m_cooldownDuration - elapsedSeconds);
            return false;
        }
    }

    // Show the notification
    RE::DebugNotification(message.data());

    // Start cooldown
    m_lastNotificationTime = now;
    m_hasShownNotification = true;

    spdlog::trace("NotificationManager: Showed '{}'", message);
    return true;
}

bool NotificationManager::IsInCooldown() const
{
    if (!m_hasShownNotification) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastNotificationTime);
    float elapsedSeconds = elapsed.count() / 1000.0f;

    return elapsedSeconds < m_cooldownDuration;
}

float NotificationManager::GetRemainingCooldown() const
{
    if (!m_hasShownNotification) {
        return 0.0f;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastNotificationTime);
    float elapsedSeconds = elapsed.count() / 1000.0f;

    float remaining = m_cooldownDuration - elapsedSeconds;
    return remaining > 0.0f ? remaining : 0.0f;
}

void NotificationManager::ResetCooldown()
{
    m_hasShownNotification = false;
    spdlog::trace("NotificationManager: Cooldown reset");
}
