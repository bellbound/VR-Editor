#pragma once

#include "../interfaces/ThreeDUIInterface001.h"
#include "../log.h"
#include "../util/MenuChecker.h"

// MenuStateManager: Owns menu roots and provides lazy initialization for menus.
//
// Menus are initialized on first use (when user tries to open them), not at game start.
// This improves startup time and ensures any initialization errors are user-triggered
// rather than causing mysterious crashes during game load.
//
// Usage:
//   // In menu Show() method:
//   if (!MenuStateManager::GetSingleton()->EnsureSelectionMenuReady()) {
//       spdlog::error("Failed to initialize menu");
//       return;
//   }
//   // ... proceed to show menu
class MenuStateManager
{
public:
    static MenuStateManager* GetSingleton();

    // Legacy Initialize - now a lightweight no-op for API compatibility
    // Actual menu setup happens lazily via EnsureXxxReady()
    bool Initialize();

    // Lazy initialization - call before showing each menu
    // Returns true if menu is ready to show, false if initialization failed
    // Safe to call multiple times - fast path if already initialized
    bool EnsureSelectionMenuReady();
    bool EnsureGalleryMenuReady();

    // Check if menus are ready (without triggering initialization)
    bool IsSelectionMenuReady() const { return m_selectionMenuReady; }
    bool IsGalleryMenuReady() const { return m_galleryMenuReady; }

    // Access roots (may be nullptr if not yet initialized)
    P3DUI::Root* GetSelectionRoot() const { return m_selectionRoot; }
    P3DUI::Root* GetGalleryRoot() const { return m_galleryRoot; }

    bool IsSelectionBlockingMenuOpen() const
    {
        return MenuChecker::IsGameStopped()
            || (m_selectionRoot && m_selectionRoot->IsVisible());
    }

private:
    MenuStateManager() = default;
    ~MenuStateManager() = default;
    MenuStateManager(const MenuStateManager&) = delete;
    MenuStateManager& operator=(const MenuStateManager&) = delete;

    P3DUI::Root* m_selectionRoot = nullptr;
    P3DUI::Root* m_galleryRoot = nullptr;

    bool m_initialized = false;           // Legacy flag
    bool m_selectionMenuReady = false;    // SelectionMenu fully setup
    bool m_galleryMenuReady = false;      // GalleryMenu fully setup
};
