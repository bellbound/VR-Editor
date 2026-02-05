#pragma once

#include <cstdint>

// =============================================================================
// HealthCheck - Validates dependency versions at runtime
// =============================================================================
// Singleton that verifies 3DUI and other dependencies are compatible.
// Call MayShowDependenciesErrorMessage() on PostLoadGame to notify users once.

class HealthCheck
{
public:
    static HealthCheck* GetSingleton()
    {
        static HealthCheck instance;
        return &instance;
    }

    // Check if all required dependencies are present and version-compatible.
    // Returns true if all dependencies are up to date.
    bool AreDependenciesUpToDate();

    // Shows a user notification if dependencies are incompatible.
    // Only displays once per session (tracks state internally).
    // Call this from OnPostLoadGame.
    void MayShowDependenciesErrorMessage();

private:
    HealthCheck() = default;
    ~HealthCheck() = default;
    HealthCheck(const HealthCheck&) = delete;
    HealthCheck& operator=(const HealthCheck&) = delete;

    // Tracks if we've shown the error notification this session
    bool m_errorMessageShown = false;

    // Cached result of dependency check (computed once)
    bool m_dependencyCheckDone = false;
    bool m_dependenciesUpToDate = true;

    // Helper to extract version components from packed format
    static void UnpackVersion(uint32_t version, uint32_t& major, uint32_t& minor, uint32_t& patch, uint32_t& build);

    // Checks if actual version is compatible with expected version
    // Pre-1.0: major and minor must match exactly
    // Post-1.0: only major version must match (minor can be >= expected)
    static bool IsVersionCompatible(uint32_t actualVersion, uint32_t expectedVersion);
};
