#pragma once

#include <RE/Skyrim.h>
#include <memory>
#include <string>
#include <Windows.h>
#include "../log.h"
#include "../interfaces/ThreeDUIInterface001.h"
#include "../gallery/GalleryManager.h"
#include "../actions/UndoRedoController.h"
#include "../actions/CopyHandler.h"
#include "../actions/ActionHistoryRepository.h"
#include "../persistence/ChangedObjectRegistry.h"
#include "../persistence/CreatedObjectTracker.h"
#include "../persistence/FormKeyUtil.h"
#include "../util/UUID.h"
#include "openvr.h"
#include "../EditModeInputManager.h"
#include "MenuStateManager.h"

// GalleryMenu: Standalone 3D menu for browsing and placing gallery items
// Separate from SelectionMenu with its own root
// Activated via gallery button on SelectionMenu
class GalleryMenu
{
public:
    static GalleryMenu* GetSingleton()
    {
        static GalleryMenu instance;
        return &instance;
    }

    // Initialize the 3D UI subsystems
    bool Initialize()
    {
        if (m_initialized) {
            spdlog::warn("GalleryMenu::Initialize - Already initialized");
            return true;
        }

        // Get the 3D UI interface
        m_api = P3DUI::GetInterface001();
        if (!m_api) {
            spdlog::error("GalleryMenu::Initialize - Failed to get 3D UI interface");
            return false;
        }

        spdlog::info("GalleryMenu::Initialize - 3D UI interface acquired");
        m_initialized = true;
        return true;
    }

    void Shutdown()
    {
        // Clean up trigger callback
        UnregisterTriggerCallback();
        m_isPositioning = false;

        if (auto* root = GetRoot()) {
            root->SetVisible(false);
        }
        m_initialized = false;
    }

    // Called by EditModeTransitioner when exiting edit mode
    void OnEditModeExit()
    {
        // Clean up positioning state if still positioning
        if (m_isPositioning) {
            if (auto* root = GetRoot()) {
                root->EndPositioning();
            }
            m_isPositioning = false;
            UnregisterTriggerCallback();
        }

        if (auto* root = GetRoot(); root && root->IsVisible()) {
            root->SetVisible(false);
            spdlog::info("GalleryMenu::OnEditModeExit - Menu hidden");
        }
    }

    // One-time setup (called by MenuStateManager during PostLoadGame)
    bool SetupMenu()
    {
        if (m_menuSetup) {
            return true;  // Already setup
        }

        if (!m_initialized || !m_api) {
            spdlog::error("GalleryMenu::SetupMenu - Not initialized");
            return false;
        }

        auto* root = GetRoot();
        if (!root) {
            spdlog::error("GalleryMenu::SetupMenu - Menu root not initialized");
            return false;
        }

        spdlog::info("GalleryMenu::SetupMenu - Creating menu via public API");

        // === Set initial position based on HMD ===
        PositionMenuAtHMDForward();

        // Configure VR anchor (HMD for facing) and face the player
        root->SetVRAnchor(P3DUI::VRAnchorType::HMD);
        root->SetFacingMode(P3DUI::FacingMode::Full);

        // === Create Gallery ScrollWheel ===
        P3DUI::ScrollWheelConfig wheelConfig = P3DUI::ScrollWheelConfig::Default("gallery_wheel");
        wheelConfig.itemSpacing = 12.0f;
        wheelConfig.ringSpacing = 8.0f;
        wheelConfig.firstRingSpacing = 15.0f;

        m_galleryWheel = m_api->CreateScrollWheel(wheelConfig);
        if (m_galleryWheel) {
            root->AddChild(m_galleryWheel);
        }


        // === Create Item Count Text ===
        P3DUI::TextConfig textConfig = P3DUI::TextConfig::Default("gallery_item_count_text");
        textConfig.text = L"";  // Initially empty
        textConfig.scale = 1.0f;
        textConfig.facingMode = P3DUI::FacingMode::YawOnly;

        m_itemCountText = m_api->CreateText(textConfig);
        if (m_itemCountText) {
            root->AddChild(m_itemCountText);
            m_itemCountText->SetLocalPosition(0, 0, -8.0f);  // Position below tool row
        }

        spdlog::info("GalleryMenu::SetupMenu - Setup complete");
        m_menuSetup = true;
        return true;
    }

    void Show(bool isLeftHand)
    {
        if (IsMenuOpen()) return;

        // Lazy initialization - setup menu on first show
        if (!m_menuSetup) {
            if (!MenuStateManager::GetSingleton()->EnsureGalleryMenuReady()) {
                spdlog::error("GalleryMenu::Show - Failed to initialize menu");
                RE::DebugNotification("[In-Game Patcher] Gallery menu failed to initialize");
                return;
            }
        }

        if (!m_initialized) return;

        spdlog::info("GalleryMenu::Show - Opening menu at {} hand for positioning",
            isLeftHand ? "left" : "right");

        // Track which hand is positioning
        m_positioningHand = isLeftHand;
        m_isPositioning = true;

        // Populate gallery wheel with current gallery contents (snapshot)
        PopulateGalleryWheel();

        // Update item count text
        UpdateItemCountText();

        // Populate tool row (currently empty)
        PopulateToolRow();

        // Show menu
        if (auto* root = GetRoot()) {
            root->SetVisible(true);
        }

        // Start positioning - attach menu to the hand that opened it
        if (auto* root = GetRoot()) {
            root->StartPositioning(isLeftHand);
        }

        // Register trigger callback to place the gallery
        RegisterTriggerCallback();

        spdlog::info("GalleryMenu::Show - Menu opened, awaiting trigger press to place");
    }

    // Close the gallery menu - does NOT exit edit mode
    void Close()
    {
        auto* root = GetRoot();
        if (!root || !root->IsVisible()) {
            return;
        }

        // Clean up positioning state if still positioning
        if (m_isPositioning) {
            root->EndPositioning();
            m_isPositioning = false;
            UnregisterTriggerCallback();
        }

        // Hide menu
        root->SetVisible(false);

        spdlog::info("GalleryMenu::Close - Menu closed (edit mode continues)");
    }

    bool IsMenuOpen() const
    {
        if (auto* root = GetRoot()) {
            return root->IsVisible();
        }
        return false;
    }

    // Static event callback - routes to singleton instance
    static bool OnEvent(const P3DUI::Event* event)
    {
        return GetSingleton()->HandleEvent(event);
    }

private:
    GalleryMenu() = default;
    ~GalleryMenu() = default;
    GalleryMenu(const GalleryMenu&) = delete;
    GalleryMenu& operator=(const GalleryMenu&) = delete;

    // Position the menu relative to HMD: forward and at hip height
    void PositionMenuAtHMDForward()
    {
        auto* root = GetRoot();
        if (!root) return;

        // Position is relative to HMD since we use VRAnchorType::HMD
        // X = left/right, Y = forward/back, Z = up/down
        constexpr float kForwardDistance = 20.0f;   // Forward from HMD
        constexpr float kHipHeightOffset = -45.0f;  // Below HMD (hip height)

        root->SetLocalPosition(0.0f, kForwardDistance, kHipHeightOffset);

        spdlog::info("GalleryMenu::PositionMenuAtHMDForward - Position set to (0, {:.1f}, {:.1f}) relative to HMD",
            kForwardDistance, kHipHeightOffset);
    }

    // Update the item count text
    void UpdateItemCountText()
    {
        if (!m_itemCountText) return;

        auto* gallery = Gallery::GalleryManager::GetSingleton();
        size_t count = gallery->GetCount();

        if (count == 0) {
            m_itemCountText->SetText(L"No Items");
        } else if (count == 1) {
            m_itemCountText->SetText(L"1 Item");
        } else {
            wchar_t buffer[64];
            swprintf_s(buffer, 64, L"%zu Items", count);
            m_itemCountText->SetText(buffer);
        }
        m_itemCountText->SetVisible(true);
    }

    // Instance event handler
    bool HandleEvent(const P3DUI::Event* event)
    {
        if (!event || !event->sourceID) return false;

        std::string id(event->sourceID);

        // Handle activation events (key down)
        if (event->type == P3DUI::EventType::ActivateDown) {
            // Central orb - close gallery menu only (does NOT exit edit mode)
            if (id == "gallery_central_orb") {
                spdlog::info("GalleryMenu: Central orb activated - closing gallery menu");
                Close();
                return true;
            }

            // Gallery item click - place object
            if (id.rfind("gallery_item_", 0) == 0) {  // starts_with
                std::string indexStr = id.substr(13);  // after "gallery_item_"
                try {
                    size_t index = std::stoul(indexStr);
                    PlaceGalleryItem(index);
                } catch (const std::exception& e) {
                    spdlog::error("GalleryMenu: Failed to parse gallery item index: {}", e.what());
                }
                return true;
            }
        }

        return false;
    }

    // Place a gallery item by index
    void PlaceGalleryItem(size_t index)
    {
        auto* gallery = Gallery::GalleryManager::GetSingleton();
        const auto items = gallery->GetSortedObjects();

        if (index >= items.size()) {
            spdlog::error("GalleryMenu: Invalid gallery item index {}", index);
            return;
        }

        const auto& item = items[index];
        spdlog::info("GalleryMenu: Placing gallery item {} - '{}'", index, item.displayName);

        auto* placed = gallery->PlaceObject(item);
        if (placed) {
            // Record undo action for gallery placement
            Util::ActionId actionId = Util::UUID::Generate();

            // Build transform from placed object
            RE::NiTransform transform;
            if (auto* node = placed->Get3D()) {
                transform = node->world;
            } else {
                transform.translate = placed->GetPosition();
                transform.scale = 1.0f;
            }

            // Get base form ID for persistence tracking
            RE::FormID baseFormId = 0;
            if (auto* baseObj = placed->GetBaseObject()) {
                baseFormId = baseObj->GetFormID();
            }

            // Register with ChangedObjectRegistry for persistence (INI export)
            Persistence::ChangedObjectRegistry::GetSingleton()->RegisterCreatedObject(
                placed, baseFormId, transform, actionId);

            // Register with CreatedObjectTracker for runtime spawning/despawning
            auto* cell = placed->GetParentCell();
            std::string cellFormKey = cell ? Persistence::FormKeyUtil::BuildFormKey(cell) : "";
            if (!cellFormKey.empty()) {
                Persistence::CreatedObjectTracker::GetSingleton()->Add(placed, baseFormId, cellFormKey);
            }

            // Record CopyAction for undo support
            Actions::SingleCopy copy;
            copy.originalFormId = 0;  // From gallery, not a copy of existing ref
            copy.createdFormId = placed->GetFormID();
            copy.transform = transform;

            std::vector<Actions::SingleCopy> copies;
            copies.push_back(copy);
            Actions::CopyAction action(std::move(copies));
            action.actionId = actionId;
            Actions::ActionHistoryRepository::GetSingleton()->Add(std::move(action));

            spdlog::info("GalleryMenu: Recorded gallery placement action for {:08X}",
                placed->GetFormID());
        }
        // Gallery stays open - user can place multiple items
    }

    // Populate the gallery wheel with items from GalleryManager
    void PopulateGalleryWheel()
    {
        if (!m_galleryWheel || !m_api) return;

        m_galleryWheel->Clear();

        // Central orb anchor handle - closes gallery menu only
        P3DUI::ElementConfig orbConfig = P3DUI::ElementConfig::Default("gallery_central_orb");
        orbConfig.modelPath = "meshes\\3DUI\\orb.nif";
        orbConfig.scale = 1.2f;
        orbConfig.facingMode = P3DUI::FacingMode::None;
        orbConfig.isAnchorHandle = true;

        auto* orbHandle = m_api->CreateElement(orbConfig);
        if (orbHandle) {
            m_galleryWheel->AddChild(orbHandle);
        }

        // Add all gallery items as mesh previews
        auto* gallery = Gallery::GalleryManager::GetSingleton();
        const auto items = gallery->GetSortedObjects();

        for (size_t i = 0; i < items.size(); i++) {
            const auto& item = items[i];

            std::string elementId = "gallery_item_" + std::to_string(i);
            P3DUI::ElementConfig itemConfig = P3DUI::ElementConfig::Default(elementId.c_str());

            // Use the stored mesh path for 3D preview
            if (!item.meshPath.empty()) {
                itemConfig.modelPath = item.meshPath.c_str();
            } else {
                // Fallback to default mesh if no mesh path
                itemConfig.modelPath = "meshes\\3DUI\\orb.nif";
            }

            // Convert display name to wide string for tooltip
            std::wstring tooltip(item.displayName.begin(), item.displayName.end());
            itemConfig.tooltip = tooltip.c_str();
            itemConfig.scale = item.targetScale;  // Auto-calculated scale
            itemConfig.facingMode = P3DUI::FacingMode::None;

            auto* itemElement = m_api->CreateElement(itemConfig);
            if (itemElement) {
                m_galleryWheel->AddChild(itemElement);
            }
        }

        spdlog::info("GalleryMenu::PopulateGalleryWheel - Added {} gallery items + central orb",
            items.size());
    }

    // Populate the tool row (currently empty)
    void PopulateToolRow()
    {
        if (!m_toolRow || !m_api) return;

        m_toolRow->Clear();

        // Tool row is intentionally empty for now
        // Future: back button, search, filters, etc.

        spdlog::info("GalleryMenu::PopulateToolRow - Tool row is empty");
    }

    // Register trigger callback to detect placement
    void RegisterTriggerCallback()
    {
        auto* editModeInput = EditModeInputManager::GetSingleton();
        if (!editModeInput->IsInitialized()) {
            spdlog::error("GalleryMenu::RegisterTriggerCallback - EditModeInputManager not initialized!");
            return;
        }

        m_triggerCallbackId = editModeInput->AddVrButtonCallback(
            vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger),
            [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
                return this->OnTriggerPressed(isLeft, isReleased);
            }
        );
        spdlog::info("GalleryMenu::RegisterTriggerCallback - Trigger callback registered with ID {}",
            m_triggerCallbackId);
    }

    // Unregister trigger callback
    void UnregisterTriggerCallback()
    {
        if (m_triggerCallbackId != EditModeInputManager::InvalidCallbackId) {
            EditModeInputManager::GetSingleton()->RemoveVrButtonCallback(m_triggerCallbackId);
            m_triggerCallbackId = EditModeInputManager::InvalidCallbackId;
            spdlog::info("GalleryMenu::UnregisterTriggerCallback - Trigger callback unregistered");
        }
    }

    // Handle trigger press for placement
    bool OnTriggerPressed(bool isLeft, bool isReleased)
    {
        // Only handle if we're in positioning mode
        if (!m_isPositioning) {
            return false;  // Don't consume - let other handlers process
        }

        // Only respond to the hand that's positioning the menu
        if (isLeft != m_positioningHand) {
            return false;  // Different hand, don't consume
        }

        // Only respond to press (not release)
        if (isReleased) {
            return false;  // Don't consume release
        }

        spdlog::info("GalleryMenu::OnTriggerPressed - Placing gallery at current position");

        // End positioning - fix menu at current world position
        if (auto* root = GetRoot()) {
            root->EndPositioning();
        }

        // Clear positioning state
        m_isPositioning = false;

        // Unregister the callback - we're done positioning
        UnregisterTriggerCallback();

        return true;  // Consume the input
    }

    P3DUI::Interface001* m_api = nullptr;
    P3DUI::Container* m_galleryWheel = nullptr;
    P3DUI::Container* m_toolRow = nullptr;
    P3DUI::Text* m_itemCountText = nullptr;

    // Positioning state
    bool m_positioningHand = false;  // Which hand is positioning (true = left)
    bool m_isPositioning = false;    // Are we currently in positioning mode?
    EditModeInputManager::CallbackId m_triggerCallbackId = EditModeInputManager::InvalidCallbackId;

    bool m_initialized = false;
    bool m_menuSetup = false;

    P3DUI::Root* GetRoot() const
    {
        return MenuStateManager::GetSingleton()->GetGalleryRoot();
    }
};
