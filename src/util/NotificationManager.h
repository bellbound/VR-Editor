#pragma once

#include <chrono>
#include <string_view>

// NotificationManager: Throttles debug notifications to prevent spam
//
// Each notification triggers a 1-second cooldown during which all
// subsequent notifications are silently swallowed.
class NotificationManager
{
public:
    static NotificationManager* GetSingleton();

    // Show a notification (throttled)
    // Returns true if the notification was shown, false if swallowed due to cooldown
    bool Show(std::string_view message);

    // Show a notification (throttled) with format string
    template<typename... Args>
    bool Show(std::string_view format, Args&&... args) {
        // Use fmt to format the string
        std::string formatted = fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
        return Show(std::string_view(formatted));
    }

    // Check if currently in cooldown
    bool IsInCooldown() const;

    // Get remaining cooldown time in seconds (0 if not in cooldown)
    float GetRemainingCooldown() const;

    // Force reset the cooldown (for testing/special cases)
    void ResetCooldown();

    // Configurable cooldown duration (default 1.0s)
    void SetCooldownDuration(float seconds) { m_cooldownDuration = seconds; }
    float GetCooldownDuration() const { return m_cooldownDuration; }

private:
    NotificationManager() = default;
    ~NotificationManager() = default;
    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;

    std::chrono::steady_clock::time_point m_lastNotificationTime;
    bool m_hasShownNotification = false;
    float m_cooldownDuration = 1.0f;  // 1 second default
};
