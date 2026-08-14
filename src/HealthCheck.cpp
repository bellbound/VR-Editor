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

    // Pre-1.0.0: minor is the breaking lane, patch is the additive lane.
    // Major and minor must match exactly; the provider's patch must be at least
    // what we were built against, because a newer patch only ever adds methods in
    // reserved vtable slots. This is what lets a 3DUI patch release stay a drop-in
    // upgrade for consumers built before it, while still refusing a provider too
    // old to implement the methods we actually call.
    // (0.10.1.0 provider is OK for a 0.10.0.0 consumer; 0.10.0.0 provider is NOT OK
    //  for a 0.10.1.0 consumer; 0.10.x and 0.9.x are never compatible either way.)
    if (expectedMajor < 1) {
        return (actualMajor == expectedMajor)
            && (actualMinor == expectedMinor)
            && (actualPatch >= expectedPatch);
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

    // Check 3DUI interface. Deliberately do NOT latch when 3DUI is merely absent:
    // this can be reached before 3DUI has registered its interface, and latching a
    // negative there would disable VR Editor for the whole session over a startup
    // race. Re-checking is cheap - the interface pointer is cached once acquired.
    auto* p3dui = P3DUI::GetInterface001();
    if (!p3dui) {
        spdlog::warn("HealthCheck: 3DUI interface not available (not latching, will re-check)");
        return false;
    }

    // 3DUI answered, so its version is final for this session - safe to latch.
    m_dependencyCheckDone = true;
    m_dependenciesUpToDate = true;

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

bool HealthCheck::IsFunctionalityDisabled()
{
    // Trigger check if not done yet, then return inverse
    return !AreDependenciesUpToDate();
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

    // Show message box and mark as shown
    m_errorMessageShown = true;

    auto* p3dui = P3DUI::GetInterface001();
    if (!p3dui) {
        RE::DebugMessageBox("VR Editor: Required 3DUI.dll is missing!\n\nVR Editor functionality has been disabled. Please install 3DUI.");
        spdlog::error("HealthCheck: Displayed message box - 3DUI missing. Functionality disabled.");
    } else {
        uint32_t actualMajor, actualMinor, actualPatch, actualBuild;
        uint32_t expectedMajor, expectedMinor, expectedPatch, expectedBuild;

        UnpackVersion(p3dui->GetInterfaceVersion(), actualMajor, actualMinor, actualPatch, actualBuild);
        UnpackVersion(P3DUI::P3DUI_INTERFACE_VERSION, expectedMajor, expectedMinor, expectedPatch, expectedBuild);

        // Name the actual problem. Too old is the common case and the user can fix
        // it by updating 3DUI; too new means their VR Editor is the stale one.
        const bool tooOld = p3dui->GetInterfaceVersion() < P3DUI::P3DUI_INTERFACE_VERSION;

        auto msg = fmt::format(
            "VR Editor: your 3DUI is {}!\n\n"
            "Installed 3DUI: {}.{}.{}.{}\n"
            "Required 3DUI:  {}.{}.{}.{} or newer\n\n"
            "VR Editor has shut itself down for this session to avoid\n"
            "misbehaving. Nothing else has been changed and the rest of\n"
            "your game is unaffected.\n\n"
            "{}",
            tooOld ? "too old for this version of VR Editor" : "newer than this version of VR Editor expects",
            actualMajor, actualMinor, actualPatch, actualBuild,
            expectedMajor, expectedMinor, expectedPatch, expectedBuild,
            tooOld ? "Please update 3DUI, then restart the game."
                   : "Please update VR Editor, then restart the game.");

        RE::DebugMessageBox(msg.c_str());
        spdlog::error("HealthCheck: Displayed message box - 3DUI {} ({}.{}.{}.{} installed, {}.{}.{}.{} required). Functionality disabled for this session.",
            tooOld ? "too old" : "too new",
            actualMajor, actualMinor, actualPatch, actualBuild,
            expectedMajor, expectedMinor, expectedPatch, expectedBuild);
    }
}
