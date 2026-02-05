#include "HealthCheck.h"
#include "log.h"
#include "interfaces/ThreeDUIInterface001.h"

void HealthCheck::UnpackVersion(uint32_t version, uint32_t& major, uint32_t& minor, uint32_t& patch, uint32_t& build)
{
    // Version format: Major * 1000000 + Minor * 10000 + Patch * 100 + Build
    major = version / 1000000;
    minor = (version / 10000) % 100;
    patch = (version / 100) % 100;
    build = version % 100;
}

bool HealthCheck::IsVersionCompatible(uint32_t actualVersion, uint32_t expectedVersion)
{
    uint32_t actualMajor, actualMinor, actualPatch, actualBuild;
    uint32_t expectedMajor, expectedMinor, expectedPatch, expectedBuild;

    UnpackVersion(actualVersion, actualMajor, actualMinor, actualPatch, actualBuild);
    UnpackVersion(expectedVersion, expectedMajor, expectedMinor, expectedPatch, expectedBuild);

    // Pre-1.0.0: API is unstable, major AND minor must match exactly
    // (e.g., 0.10.1.0 and 0.10.0.0 is OK, 0.10.x and 0.9.x is NOT)
    if (expectedMajor < 1) {
        return (actualMajor == expectedMajor) && (actualMinor == expectedMinor);
    }

    // Post-1.0.0: Standard semver - backwards compatible unless major changes
    // Provider major must equal expected major, provider minor must be >= expected minor
    if (actualMajor != expectedMajor) {
        return false;
    }

    // Same major version - newer minor versions are backwards compatible
    return actualMinor >= expectedMinor;
}

bool HealthCheck::AreDependenciesUpToDate()
{
    // Return cached result if already checked
    if (m_dependencyCheckDone) {
        return m_dependenciesUpToDate;
    }

    m_dependencyCheckDone = true;
    m_dependenciesUpToDate = true;

    // Check 3DUI interface
    auto* p3dui = P3DUI::GetInterface001();
    if (!p3dui) {
        spdlog::warn("HealthCheck: 3DUI interface not available");
        m_dependenciesUpToDate = false;
        return false;
    }

    uint32_t actualVersion = p3dui->GetInterfaceVersion();
    uint32_t expectedVersion = P3DUI::P3DUI_INTERFACE_VERSION;

    if (!IsVersionCompatible(actualVersion, expectedVersion)) {
        uint32_t actualMajor, actualMinor, actualPatch, actualBuild;
        uint32_t expectedMajor, expectedMinor, expectedPatch, expectedBuild;

        UnpackVersion(actualVersion, actualMajor, actualMinor, actualPatch, actualBuild);
        UnpackVersion(expectedVersion, expectedMajor, expectedMinor, expectedPatch, expectedBuild);

        spdlog::error("HealthCheck: 3DUI version mismatch! Found {}.{}.{}.{}, expected {}.{}.{}.{}",
            actualMajor, actualMinor, actualPatch, actualBuild,
            expectedMajor, expectedMinor, expectedPatch, expectedBuild);

        m_dependenciesUpToDate = false;
        return false;
    }

    // Log successful version check
    uint32_t major, minor, patch, build;
    UnpackVersion(actualVersion, major, minor, patch, build);
    spdlog::info("HealthCheck: 3DUI version {}.{}.{}.{} is compatible", major, minor, patch, build);

    return true;
}

void HealthCheck::MayShowDependenciesErrorMessage()
{
    // Only show once per session
    if (m_errorMessageShown) {
        return;
    }

    // Check dependencies (uses cached result if already checked)
    if (AreDependenciesUpToDate()) {
        return;
    }

    // Show notification and mark as shown
    m_errorMessageShown = true;

    auto* p3dui = P3DUI::GetInterface001();
    if (!p3dui) {
        RE::DebugNotification("VR Editor: Required 3DUI.dll is missing!");
        spdlog::warn("HealthCheck: Displayed notification - 3DUI missing");
    } else {
        RE::DebugNotification("VR Editor: Incompatible 3DUI version detected!");
        RE::DebugNotification("VR Editor: Please update 3DUI to a compatible version");
        spdlog::warn("HealthCheck: Displayed notification - 3DUI version incompatible");
    }
}
