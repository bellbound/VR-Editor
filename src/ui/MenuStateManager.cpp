#include "MenuStateManager.h"
#include "SelectionMenu.h"
#include "GalleryMenu.h"
#include "openvr.h"

MenuStateManager* MenuStateManager::GetSingleton()
{
    static MenuStateManager instance;
    return &instance;
}

bool MenuStateManager::EnsureSelectionMenuReady()
{
    // Fast path - already setup
    if (m_selectionMenuReady) {
        return true;
    }

    spdlog::info("MenuStateManager: Lazy-initializing SelectionMenu...");

    try {
        // Get 3DUI interface (initializes subsystems on first call)
        auto* api = P3DUI::GetInterface001();
        if (!api) {
            spdlog::error("MenuStateManager::EnsureSelectionMenuReady - 3D UI interface not available");
            return false;
        }

        // Create root if needed
        if (!m_selectionRoot) {
            P3DUI::RootConfig rootConfig = P3DUI::RootConfig::Default("selection_menu_root", "InGamePatcherVR");
            rootConfig.interactive = true;
            rootConfig.activationButtonMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
            rootConfig.grabButtonMask = vr::ButtonMaskFromId(vr::k_EButton_Grip);
            rootConfig.eventCallback = &SelectionMenu::OnEvent;

            m_selectionRoot = api->CreateRoot(rootConfig);
            if (!m_selectionRoot) {
                spdlog::error("MenuStateManager::EnsureSelectionMenuReady - Failed to create selection menu root");
                return false;
            }
        }

        // Setup menu elements
        if (!SelectionMenu::GetSingleton()->SetupMenu()) {
            spdlog::error("MenuStateManager::EnsureSelectionMenuReady - SelectionMenu::SetupMenu failed");
            return false;
        }

        m_selectionMenuReady = true;
        spdlog::info("MenuStateManager: SelectionMenu ready");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("MenuStateManager::EnsureSelectionMenuReady - Exception: {}", e.what());
        return false;
    } catch (...) {
        spdlog::error("MenuStateManager::EnsureSelectionMenuReady - Unknown exception");
        return false;
    }
}

bool MenuStateManager::EnsureGalleryMenuReady()
{
    // Fast path - already setup
    if (m_galleryMenuReady) {
        return true;
    }

    spdlog::info("MenuStateManager: Lazy-initializing GalleryMenu...");

    try {
        // Get 3DUI interface (initializes subsystems on first call)
        auto* api = P3DUI::GetInterface001();
        if (!api) {
            spdlog::error("MenuStateManager::EnsureGalleryMenuReady - 3D UI interface not available");
            return false;
        }

        // Create root if needed
        if (!m_galleryRoot) {
            P3DUI::RootConfig rootConfig = P3DUI::RootConfig::Default("gallery_menu_root", "InGamePatcherVR");
            rootConfig.interactive = true;
            rootConfig.activationButtonMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
            rootConfig.grabButtonMask = vr::ButtonMaskFromId(vr::k_EButton_Grip);
            rootConfig.eventCallback = &GalleryMenu::OnEvent;

            m_galleryRoot = api->CreateRoot(rootConfig);
            if (!m_galleryRoot) {
                spdlog::error("MenuStateManager::EnsureGalleryMenuReady - Failed to create gallery menu root");
                return false;
            }
        }

        // Setup menu elements
        if (!GalleryMenu::GetSingleton()->SetupMenu()) {
            spdlog::error("MenuStateManager::EnsureGalleryMenuReady - GalleryMenu::SetupMenu failed");
            return false;
        }

        m_galleryMenuReady = true;
        spdlog::info("MenuStateManager: GalleryMenu ready");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("MenuStateManager::EnsureGalleryMenuReady - Exception: {}", e.what());
        return false;
    } catch (...) {
        spdlog::error("MenuStateManager::EnsureGalleryMenuReady - Unknown exception");
        return false;
    }
}

// Legacy Initialize - now a no-op, kept for API compatibility
// Menus are initialized lazily when first shown
bool MenuStateManager::Initialize()
{
    if (m_initialized) {
        return true;
    }

    // Just mark as initialized - actual setup happens lazily in EnsureXxxReady()
    spdlog::info("MenuStateManager::Initialize - Using lazy initialization (menus setup on first use)");
    m_initialized = true;
    return true;
}
