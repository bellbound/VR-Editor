#pragma once

#include <RE/Skyrim.h>
#include <memory>
#include <string>
#include <Windows.h>
#include "../log.h"
#include "../interfaces/ThreeDUIInterface001.h"
#include "../EditModeManager.h"
#include "../selection/SelectionState.h"
#include "../selection/DelayedHighlightRefreshManager.h"
#include "../actions/UndoRedoController.h"
#include "../actions/DeleteHandler.h"
#include "../actions/CopyHandler.h"
#include "../actions/ResetRotationHandler.h"
#include "../actions/ActionHistoryRepository.h"
#include "../util/VRNodes.h"
#include "../EditModeInputManager.h"
#include "../gallery/GalleryManager.h"
#include "../persistence/ChangedObjectRegistry.h"
#include "../grab/RemoteGrabController.h"
#include "SelectionMenuHelpers.h"
#include "GalleryMenu.h"
#include "openvr.h"
#include "MenuStateManager.h"

// Selection action types for the menu
enum class SelectionAction {
    None,
    Delete,
    Copy
};

// Context menu states - controls what the context-dependent wheel displays
enum class ContextMenuState {
    Hidden,           // No selection - shows only close handle orb
    SelectionMode     // Object selected - snap/copy/delete/save-to-gallery
};

// Callback when a selection action is chosen
typedef void (*SelectionActionCallback)(SelectionAction action);

class SelectionMenu
{
public:
    static SelectionMenu* GetSingleton()
    {
        static SelectionMenu instance;
        return &instance;
    }

    // Initialize the 3D UI subsystems and register B button callback
    bool Initialize()
    {
        if (m_initialized) {
            spdlog::warn("SelectionMenu::Initialize - Already initialized");
            return true;
        }

        // Get the 3D UI interface - this initializes all subsystems
        if(!m_api){
            m_api = P3DUI::GetInterface001();
            if (!m_api) {
                spdlog::error("SelectionMenu::Initialize - Failed to get 3D UI interface");
                return false;
            }
        }

        // Check for interface version compatibility
        uint32_t interfaceVersion = m_api->GetInterfaceVersion();
        if (interfaceVersion > P3DUI::P3DUI_INTERFACE_VERSION) {
            spdlog::warn("SelectionMenu::Initialize - 3DUI interface version {} is newer than expected version {}",
                interfaceVersion, P3DUI::P3DUI_INTERFACE_VERSION);
            RE::DebugNotification("[In-Game Patcher] Incompatible 3DUI Version detected");
            RE::DebugNotification("[In-Game Patcher] Please update to the latest version");
        }

        // Register B button callback for quick-select menu (edit mode only)
        auto* editModeInput = EditModeInputManager::GetSingleton();
        if (editModeInput->IsInitialized()) {
            m_bButtonCallbackId = editModeInput->AddVrButtonCallback(
                vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu),
                [this](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) {
                    return this->OnBButtonPressed(isLeft, isReleased);
                }
            );
            spdlog::info("SelectionMenu::Initialize - B button callback registered with ID {}", m_bButtonCallbackId);
        } else {
            spdlog::error("SelectionMenu::Initialize - EditModeInputManager not initialized!");
            return false;
        }

        spdlog::info("SelectionMenu::Initialize - 3D UI subsystems initialized successfully (version {})", interfaceVersion);
        m_initialized = true;
        return true;
    }

    void Shutdown()
    {
        // Unregister B button callback
        if (m_bButtonCallbackId != EditModeInputManager::InvalidCallbackId) {
            EditModeInputManager::GetSingleton()->RemoveVrButtonCallback(m_bButtonCallbackId);
            m_bButtonCallbackId = EditModeInputManager::InvalidCallbackId;
        }

        if (auto* root = GetRoot()) {
            root->SetVisible(false);
        }
        m_initialized = false;
    }

    // Called by EditModeTransitioner when entering edit mode
    // Menu no longer auto-shows - user must press B button
    void OnEditModeEnter()
    {
        // Just ensure we're initialized, don't show menu
        if (!m_initialized) {
            Initialize();
        }
        spdlog::info("SelectionMenu::OnEditModeEnter - Edit mode entered, menu ready (press B to open)");
    }

    // Called by EditModeTransitioner when exiting edit mode
    void OnEditModeExit()
    {
        // Hide the menu if open
        if (auto* root = GetRoot(); root && root->IsVisible()) {
            root->SetVisible(false);
            spdlog::info("SelectionMenu::OnEditModeExit - Menu hidden");
        }
    }

    // One-time setup (called by MenuStateManager during PostLoadGame)
    bool SetupMenu()
    {
        if (m_menuSetup) {
            return true;  // Already setup
        }

        if (!m_initialized || !m_api) {
            spdlog::error("SelectionMenu::SetupMenu - Not initialized");
            return false;
        }

        auto* root = GetRoot();
        if (!root) {
            spdlog::error("SelectionMenu::SetupMenu - Menu root not initialized");
            return false;
        }

        spdlog::info("SelectionMenu::SetupMenu - Creating menu via public API");

        // Use HMD anchor so facing mode works (menu faces the player)
        root->SetVRAnchor(P3DUI::VRAnchorType::HMD);
        root->SetFacingMode(P3DUI::FacingMode::Full);

        // === Create Context Dependent ScrollWheel ===
        P3DUI::ScrollWheelConfig wheelConfig = P3DUI::ScrollWheelConfig::Default("context_dependent_wheel");
        wheelConfig.itemSpacing = 7.0f;
        wheelConfig.ringSpacing = 6.8f;
        wheelConfig.firstRingSpacing = 10.0f;

        m_contextDependentWheel = m_api->CreateScrollWheel(wheelConfig);
        if (m_contextDependentWheel) {
            root->AddChild(m_contextDependentWheel);
        }

        // === Create Big Tool Row (for prominent actions like undo/redo) ===
        P3DUI::ColumnGridConfig bigToolRowConfig = P3DUI::ColumnGridConfig::Default("big_tool_row");
        bigToolRowConfig.numRows = 1;

        m_bigToolRow = m_api->CreateColumnGrid(bigToolRowConfig);
        if (m_bigToolRow) {
            m_bigToolRow->SetOrigin(P3DUI::VerticalOrigin::Center, P3DUI::HorizontalOrigin::Center);
            root->AddChild(m_bigToolRow);
            m_bigToolRow->SetLocalPosition(0, 0, -8.0f);  // Position below the wheel
        }

        // === Create Tool Row (using ColumnGrid with single row) ===
        P3DUI::ColumnGridConfig toolRowConfig = P3DUI::ColumnGridConfig::Default("tool_row");
        toolRowConfig.columnSpacing = 7.0f;
        toolRowConfig.numRows = 1;

        m_toolRow = m_api->CreateColumnGrid(toolRowConfig);
        if (m_toolRow) {
            m_toolRow->SetOrigin(P3DUI::VerticalOrigin::Center, P3DUI::HorizontalOrigin::Center);
            root->AddChild(m_toolRow);
            m_toolRow->SetLocalPosition(0, 0, -16.5f);  // Position below big tool row
        }

        spdlog::info("SelectionMenu::SetupMenu - Setup complete");
        m_menuSetup = true;
        return true;
    }

    // Show the menu at the specified hand position
    void ShowAtHand(bool isLeftHand)
    {
        if (IsMenuOpen()) return;

        // Lazy initialization - setup menu on first show
        if (!m_menuSetup) {
            if (!MenuStateManager::GetSingleton()->EnsureSelectionMenuReady()) {
                spdlog::error("SelectionMenu::ShowAtHand - Failed to initialize menu");
                RE::DebugNotification("[In-Game Patcher] Menu failed to initialize");
                return;
            }
        }

        if (!m_initialized) return;

        // Remember which hand opened the menu
        m_menuOpenedByLeftHand = isLeftHand;

        // Position at hand
        PositionMenuAtHand(isLeftHand);

        spdlog::info("SelectionMenu::ShowAtHand - Opening menu at {} hand",
            isLeftHand ? "left" : "right");

        // Reset to initial state
        m_contextMenuState = ContextMenuState::Hidden;

        // Populate menus based on current selection
        auto* selectionState = Selection::SelectionState::GetSingleton();
        if (selectionState->GetSelectionCount() > 0) {
            m_contextMenuState = ContextMenuState::SelectionMode;
            PopulateSelectionModeWheel();
        } else {
            // No selection - show just the close orb
            PopulateHiddenModeWheel();
        }
        PopulateToolRow();

        // Update visibility based on current selection state
        UpdateSelectionMenuVisibility();

        // Show menu
        if (auto* root = GetRoot()) {
            root->SetVisible(true);
        }

        spdlog::info("SelectionMenu::ShowAtHand - Menu opened");
    }

    // Hide the menu (does NOT exit edit mode)
    void Hide()
    {
        auto* root = GetRoot();
        if (!root || !root->IsVisible()) {
            return;
        }

        root->SetVisible(false);
        spdlog::info("SelectionMenu::Hide - Menu hidden");
    }

    // Update context dependent wheel visibility based on SelectionState
    void UpdateSelectionMenuVisibility()
    {
        auto* root = GetRoot();
        if (!root || !root->IsVisible()) return;

        auto* selectionState = Selection::SelectionState::GetSingleton();
        size_t count = selectionState->GetSelectionCount();

        // Auto-transition between Hidden and SelectionMode based on selection
        ContextMenuState newState = (count > 0)
            ? ContextMenuState::SelectionMode
            : ContextMenuState::Hidden;

        if (newState != m_contextMenuState) {
            SetContextMenuState(newState);
        }

        // Wheel is always visible
        if (m_contextDependentWheel) {
            m_contextDependentWheel->SetVisible(true);
        }

        // Always refresh the save button when in selection mode
        if (m_contextMenuState == ContextMenuState::SelectionMode) {
            RefreshSaveToGalleryButton();
        }

        // Tool rows are always visible when menu is open
        if (m_bigToolRow) {
            m_bigToolRow->SetVisible(true);
        }
        if (m_toolRow) {
            m_toolRow->SetVisible(true);
        }

        // Update gallery button visibility
        UpdateGalleryButtonVisibility();

        spdlog::info("SelectionMenu::UpdateSelectionMenuVisibility - count: {}, state: {}",
            count, static_cast<int>(m_contextMenuState));
    }

    // Set the context menu state and repopulate the wheel
    void SetContextMenuState(ContextMenuState newState)
    {
        if (m_contextMenuState == newState) return;

        m_contextMenuState = newState;

        switch (newState) {
            case ContextMenuState::Hidden:
                PopulateHiddenModeWheel();
                if (m_contextDependentWheel) {
                    m_contextDependentWheel->SetVisible(true);
                }
                break;

            case ContextMenuState::SelectionMode:
                PopulateSelectionModeWheel();
                if (m_contextDependentWheel) {
                    m_contextDependentWheel->SetVisible(true);
                }
                break;
        }

        // Update tool row
        PopulateToolRow();

        spdlog::info("SelectionMenu::SetContextMenuState - transitioned to state {}",
            static_cast<int>(newState));
    }

    // Update gallery button visibility in tool row
    void UpdateGalleryButtonVisibility()
    {
        auto* gallery = Gallery::GalleryManager::GetSingleton();
        bool showGalleryButton = !gallery->IsEmpty();

        if (m_galleryButton) {
            m_galleryButton->SetVisible(showGalleryButton);
        }
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

    // Set callback for when a selection action is chosen
    void SetSelectionActionCallback(SelectionActionCallback callback)
    {
        m_selectionActionCallback = callback;
    }

private:
    SelectionMenu() = default;
    ~SelectionMenu() = default;
    SelectionMenu(const SelectionMenu&) = delete;
    SelectionMenu& operator=(const SelectionMenu&) = delete;

    // B button press/release handler for quick-select behavior
    // Note: Only called when in edit mode (filtered by EditModeInputManager)
    bool OnBButtonPressed(bool isLeft, bool isReleased)
    {
        spdlog::info("SelectionMenu::OnBButtonPressed - isLeft: {}, isReleased: {}", isLeft, isReleased);

        if (!isReleased) {
            // B button pressed - show menu at hand
            spdlog::info("SelectionMenu::OnBButtonPressed - Showing menu at hand");
            ShowAtHand(isLeft);
            return true;  // Consume input
        } else {
            // B button released - check hover and execute action
            if (IsMenuOpen()) {
                // Get currently hovered item using the hand that opened the menu
                if (m_api) {
                    P3DUI::Positionable* hovered = m_api->GetHoveredItem(m_menuOpenedByLeftHand);

                    if (hovered) {
                        const char* id = hovered->GetID();
                        if (id) {
                            spdlog::info("SelectionMenu::OnBButtonPressed - Released while hovering '{}'", id);
                            ExecuteActionById(id);
                        }
                    } else {
                        spdlog::info("SelectionMenu::OnBButtonPressed - Released with no hover, closing menu");
                    }
                }

                // Always hide menu on B release
                Hide();
                return true;  // Consume input
            }
        }

        return false;
    }

    // Execute an action based on element ID (extracted from HandleEvent logic)
    void ExecuteActionById(const std::string& id)
    {
        // Central orb - do nothing (just closes menu)
        if (id == "close_handle") {
            spdlog::info("SelectionMenu: Orb selected - closing menu only");
            return;
        }

        // === Context Dependent Menu Actions (SelectionMode) ===
        if (id == "action_delete") {
            spdlog::info("SelectionMenu: Delete action selected");
            Actions::DeleteHandler::GetSingleton()->DeleteSelection();
            return;
        }
        if (id == "action_copy") {
            spdlog::info("SelectionMenu: Copy action selected");
            Actions::CopyHandler::GetSingleton()->CopySelection();
            return;
        }
        if (id == "action_reset_rotation") {
            spdlog::info("SelectionMenu: Reset Rotation action selected");
            Actions::ResetRotationHandler::GetSingleton()->ResetSelection();
            return;
        }

        // === Gallery Actions (SelectionMode) ===
        if (id == "action_save_to_gallery") {
            spdlog::info("SelectionMenu: Save to Gallery action selected");
            auto* selState = Selection::SelectionState::GetSingleton();
            auto* selected = selState->GetFirstSelected();
            if (selected) {
                auto* gallery = Gallery::GalleryManager::GetSingleton();
                gallery->AddObject(selected);
            }
            return;
        }
        if (id == "action_remove_from_gallery") {
            spdlog::info("SelectionMenu: Remove from Gallery action selected");
            auto* selState = Selection::SelectionState::GetSingleton();
            auto* selected = selState->GetFirstSelected();
            if (selected) {
                auto* gallery = Gallery::GalleryManager::GetSingleton();
                std::string meshPath = gallery->GetMeshPath(selected);
                gallery->RemoveObject(meshPath);
            }
            return;
        }

        // === Tool Row Actions ===
        if (id == "tool_close") {
            spdlog::info("SelectionMenu: Close tool selected - exiting edit mode");
            EditModeManager::GetSingleton()->Exit();
            return;
        }
        if (id == "tool_gallery") {
            spdlog::info("SelectionMenu: Gallery tool selected - opening GalleryMenu at {} hand",
                m_menuOpenedByLeftHand ? "left" : "right");
            GalleryMenu::GetSingleton()->Show(m_menuOpenedByLeftHand);
            return;
        }
        if (id == "tool_undo") {
            spdlog::info("SelectionMenu: Undo tool selected");
            Actions::UndoRedoController::GetSingleton()->PerformUndo();
            return;
        }
        if (id == "tool_redo") {
            spdlog::info("SelectionMenu: Redo tool selected");
            Actions::UndoRedoController::GetSingleton()->PerformRedo();
            return;
        }
        if (id == "tool_group_move") {
            bool newState = !Grab::RemoteGrabController::IsGroupMoveEnabled();
            Grab::RemoteGrabController::SetGroupMoveEnabled(newState);
            spdlog::info("SelectionMenu: Group Move toggled to {}", newState ? "ON" : "OFF");
            return;
        }
        if (id == "tool_snap_to_grid") {
            bool newState = !Grab::RemoteGrabController::IsSnapToGridEnabled();
            Grab::RemoteGrabController::SetSnapToGridEnabled(newState);
            // Clear grid override when disabling snap-to-grid
            if (!newState) {
                Grab::RemoteGrabController::GetSingleton()->GetSnapController().ClearGridOverride();
            }
            // Refresh tool row to show/hide grid align button
            PopulateToolRow();
            spdlog::info("SelectionMenu: Snap to Grid toggled to {}", newState ? "ON" : "OFF");
            return;
        }
        if (id == "tool_grid_align") {
            auto& snapController = Grab::RemoteGrabController::GetSingleton()->GetSnapController();

            if (snapController.HasGridOverride()) {
                // Toggle off - clear the override
                snapController.ClearGridOverride();
                // Refresh tool row to update button icon
                PopulateToolRow();
                spdlog::info("SelectionMenu: Grid alignment override disabled");
            } else {
                // Check if we have exactly 2 selections
                auto* selState = Selection::SelectionState::GetSingleton();
                const auto& selections = selState->GetSelection();

                if (selections.size() == 2) {
                    // Get positions and rotation of the two selected objects
                    RE::NiPoint3 posA = selections[0].ref->GetPosition();
                    RE::NiPoint3 posB = selections[1].ref->GetPosition();
                    float rotationA = selections[0].ref->GetAngleZ();

                    // Compute and set the grid override
                    auto override = Grab::SnapToGridController::ComputeGridOverride(posA, posB, rotationA);
                    snapController.SetGridOverride(override);

                    // Refresh tool row to update button icon
                    PopulateToolRow();
                    spdlog::info("SelectionMenu: Grid aligned to selection (scale={:.2f}, rotOffset={:.2f})",
                        override.scale, override.rotationOffset);
                } else {
                    // Wrong selection count - do nothing (button should be disabled visually)
                    spdlog::info("SelectionMenu: Grid align requires exactly 2 selections, got {}", selections.size());
                }
            }
            return;
        }

        spdlog::info("SelectionMenu: Unknown element '{}' selected, ignoring", id);
    }

    // Position the menu at the specified hand's position relative to HMD
    void PositionMenuAtHand(bool isLeftHand)
    {
        auto* root = GetRoot();
        if (!root) return;

        auto* hand = isLeftHand ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();
        auto* hmd = VRNodes::GetHMD();
        if (!hand || !hmd) {
            spdlog::warn("SelectionMenu::PositionMenuAtHand - Could not get {} hand or HMD node",
                isLeftHand ? "left" : "right");
            return;
        }

        // Calculate hand position relative to HMD (since VRAnchor is HMD)
        RE::NiPoint3 handWorld = hand->world.translate;
        RE::NiPoint3 hmdWorld = hmd->world.translate;
        RE::NiPoint3 relativePos = handWorld - hmdWorld;

        // Move 1.5 units closer to the player (HMD)
        float distance = std::sqrt(relativePos.x * relativePos.x + relativePos.y * relativePos.y + relativePos.z * relativePos.z);
        if (distance > 1.5f) {
            float scale = (distance - 1.5f) / distance;
            relativePos.x *= scale;
            relativePos.y *= scale;
            relativePos.z *= scale;
        }

        // Set position relative to HMD
        root->SetLocalPosition(relativePos.x, relativePos.y, relativePos.z);

        spdlog::info("SelectionMenu::PositionMenuAtHand - Position set to ({:.1f}, {:.1f}, {:.1f}) relative to HMD",
            relativePos.x, relativePos.y, relativePos.z);
    }

    // Instance event handler (for direct click events, though quick-select uses B release)
    bool HandleEvent(const P3DUI::Event* event)
    {
        if (!event || !event->sourceID) return false;

        std::string id(event->sourceID);

        // Handle activation events (direct trigger click)
        if (event->type == P3DUI::EventType::ActivateDown) {
            // For quick-select menu, most actions are handled on B release
            // But we still handle direct clicks for compatibility

            // Central orb - just close menu (don't exit edit mode)
            if (id == "close_handle") {
                spdlog::info("SelectionMenu: Orb clicked - closing menu");
                Hide();
                return true;
            }

            // Execute the action and close menu
            ExecuteActionById(id);
            Hide();
            return true;
        }

        return false;
    }

    void NotifySelectionAction(SelectionAction action)
    {
        if (m_selectionActionCallback) {
            m_selectionActionCallback(action);
        }
    }

    // Populate the context dependent scroll wheel for hidden mode (no selection)
    void PopulateHiddenModeWheel()
    {
        if (!m_contextDependentWheel || !m_api) return;

        m_contextDependentWheel->Clear();

        // Empty element - placeholder to maintain wheel layout
        P3DUI::ElementConfig emptyConfig = P3DUI::ElementConfig::Default("empty_center");
        emptyConfig.modelPath = "meshes/3DUI/empty.nif";
        emptyConfig.scale = 1.1f;
        emptyConfig.facingMode = P3DUI::FacingMode::None;
        emptyConfig.isAnchorHandle = true;

        auto* emptyElement = m_api->CreateElement(emptyConfig);
        if (emptyElement) {
            m_contextDependentWheel->AddChild(emptyElement);
        }

        spdlog::info("SelectionMenu::PopulateHiddenModeWheel - Added empty center placeholder");
    }

    // Populate the context dependent scroll wheel for selection mode
    void PopulateSelectionModeWheel()
    {
        if (!m_contextDependentWheel || !m_api) return;

        m_contextDependentWheel->Clear();

        // Empty element - placeholder to maintain wheel layout
        P3DUI::ElementConfig emptyConfig = P3DUI::ElementConfig::Default("empty_center");
        emptyConfig.modelPath = "meshes/3DUI/empty.nif";
        emptyConfig.scale = 1.1f;
        emptyConfig.facingMode = P3DUI::FacingMode::None;
        emptyConfig.isAnchorHandle = true;

        auto* emptyElement = m_api->CreateElement(emptyConfig);
        if (emptyElement) {
            m_contextDependentWheel->AddChild(emptyElement);
        }

        // Copy/Duplicate button
        P3DUI::ElementConfig copyConfig = P3DUI::ElementConfig::Default("action_copy");
        copyConfig.texturePath = "textures\\VREditor\\copy.dds";
        copyConfig.tooltip = L"Duplicate";
        copyConfig.scale = 1.1f;
        copyConfig.facingMode = P3DUI::FacingMode::None;

        auto* copyButton = m_api->CreateElement(copyConfig);
        if (copyButton) {
            m_contextDependentWheel->AddChild(copyButton);
        }

        // Delete button
        P3DUI::ElementConfig deleteConfig = P3DUI::ElementConfig::Default("action_delete");
        deleteConfig.texturePath = "textures\\VREditor\\trash.dds";
        deleteConfig.tooltip = L"Disable";
        deleteConfig.scale = 1.1f;
        deleteConfig.facingMode = P3DUI::FacingMode::None;

        auto* deleteButton = m_api->CreateElement(deleteConfig);
        if (deleteButton) {
            m_contextDependentWheel->AddChild(deleteButton);
        }

        // Reset Rotation button
        P3DUI::ElementConfig resetRotConfig = P3DUI::ElementConfig::Default("action_reset_rotation");
        resetRotConfig.texturePath = "textures\\VREditor\\reset-rotation.dds";
        resetRotConfig.tooltip = L"Reset Rotation";
        resetRotConfig.scale = 1.1f;
        resetRotConfig.facingMode = P3DUI::FacingMode::None;

        auto* resetRotButton = m_api->CreateElement(resetRotConfig);
        if (resetRotButton) {
            m_contextDependentWheel->AddChild(resetRotButton);
        }

        // Save to Gallery / Remove from Gallery button (single selection only)
        auto* selectionState = Selection::SelectionState::GetSingleton();
        size_t selectionCount = selectionState->GetSelectionCount();

        if (selectionCount == 1) {
            auto* selected = selectionState->GetFirstSelected();
            auto* gallery = Gallery::GalleryManager::GetSingleton();
            bool isInGallery = selected && gallery->IsInGallery(selected);

            P3DUI::ElementConfig galleryActionConfig = P3DUI::ElementConfig::Default(
                isInGallery ? "action_remove_from_gallery" : "action_save_to_gallery");
            galleryActionConfig.texturePath = isInGallery
                ? "textures\\VREditor\\save_highlight.dds"
                : "textures\\VREditor\\save.dds";
            galleryActionConfig.tooltip = isInGallery ? L"Remove from Gallery" : L"Save to Gallery";
            galleryActionConfig.scale = 1.1f;
            galleryActionConfig.facingMode = P3DUI::FacingMode::None;

            m_saveToGalleryButton = m_api->CreateElement(galleryActionConfig);
            if (m_saveToGalleryButton) {
                m_contextDependentWheel->AddChild(m_saveToGalleryButton);
            }
        } else {
            m_saveToGalleryButton = nullptr;
        }

        spdlog::info("SelectionMenu::PopulateSelectionModeWheel - Added {} action buttons + close handle",
            selectionCount == 1 ? 5 : 4);
    }

    // Refresh the save/remove gallery button
    void RefreshSaveToGalleryButton()
    {
        if (m_contextMenuState != ContextMenuState::SelectionMode) return;
        PopulateSelectionModeWheel();
    }

    // Populate the tool rows with utility buttons
    // Big Tool Row: [Undo] [Redo] (larger, more prominent)
    // Tool Row: [Close] [Gallery] [GroupMove] [SnapToGrid]
    void PopulateToolRow()
    {
        if (!m_toolRow || !m_bigToolRow || !m_api) return;

        m_toolRow->Clear();
        m_bigToolRow->Clear();
        m_galleryButton = nullptr;
        m_groupMoveButton = nullptr;
        m_snapToGridButton = nullptr;
        m_gridAlignButton = nullptr;

        // === Big Tool Row (Undo/Redo) ===
        constexpr float bigButtonScale = 1.53f;
        constexpr float bigButtonHoverThreshold = 12.0f;

        // Undo button (big)
        P3DUI::ElementConfig undoConfig = P3DUI::ElementConfig::Default("tool_undo");
        undoConfig.texturePath = "textures\\VREditor\\undo.dds";
        undoConfig.tooltip = L"Undo (A, A)";
        undoConfig.scale = bigButtonScale;
        undoConfig.hoverThreshold = bigButtonHoverThreshold;
        undoConfig.facingMode = P3DUI::FacingMode::None;

        auto* undoButton = m_api->CreateElement(undoConfig);
        if (undoButton) {
            m_bigToolRow->AddChild(undoButton);
        }

        // Close handle (orb) - central anchor between undo/redo
        P3DUI::ElementConfig orbConfig = P3DUI::ElementConfig::Default("close_handle");
        orbConfig.modelPath = "meshes\\3DUI\\orb.nif";
        orbConfig.scale = 1.1f;
        orbConfig.facingMode = P3DUI::FacingMode::None;
        orbConfig.isAnchorHandle = true;

        auto* closeHandle = m_api->CreateElement(orbConfig);
        if (closeHandle) {
            m_bigToolRow->AddChild(closeHandle);
        }

        // Redo button (big)
        P3DUI::ElementConfig redoConfig = P3DUI::ElementConfig::Default("tool_redo");
        redoConfig.texturePath = "textures\\VREditor\\redo.dds";
        redoConfig.tooltip = L"Redo (B, B)";
        redoConfig.scale = bigButtonScale;
        redoConfig.hoverThreshold = bigButtonHoverThreshold;
        redoConfig.facingMode = P3DUI::FacingMode::None;

        auto* redoButton = m_api->CreateElement(redoConfig);
        if (redoButton) {
            m_bigToolRow->AddChild(redoButton);
        }

        // === Tool Row (other buttons) ===

        // Close button - exits edit mode
        P3DUI::ElementConfig closeConfig = P3DUI::ElementConfig::Default("tool_close");
        closeConfig.texturePath = "textures\\VREditor\\close.dds";
        closeConfig.tooltip = L"Exit Edit Mode";
        closeConfig.scale = 1.1f;
        closeConfig.facingMode = P3DUI::FacingMode::None;

        auto* closeButton = m_api->CreateElement(closeConfig);
        if (closeButton) {
            m_toolRow->AddChild(closeButton);
        }

        auto* gallery = Gallery::GalleryManager::GetSingleton();
        bool hasGalleryItems = !gallery->IsEmpty();

        // Gallery button - opens standalone GalleryMenu (only shown when gallery has items)
        if (hasGalleryItems) {
            P3DUI::ElementConfig galleryConfig = P3DUI::ElementConfig::Default("tool_gallery");
            galleryConfig.texturePath = "textures\\VREditor\\gallery.dds";
            galleryConfig.tooltip = L"Open Gallery";
            galleryConfig.scale = 1.1f;
            galleryConfig.facingMode = P3DUI::FacingMode::None;

            m_galleryButton = m_api->CreateElement(galleryConfig);
            if (m_galleryButton) {
                m_toolRow->AddChild(m_galleryButton);
            }
        }

        // Group Move toggle button
        bool groupMoveEnabled = Grab::RemoteGrabController::IsGroupMoveEnabled();
        P3DUI::ElementConfig groupMoveConfig = P3DUI::ElementConfig::Default("tool_group_move");
        groupMoveConfig.texturePath = groupMoveEnabled
            ? "textures\\VREditor\\group-move_highlight.dds"
            : "textures\\VREditor\\group-move.dds";
        groupMoveConfig.tooltip = L"Auto-select touching physics objects";
        groupMoveConfig.scale = 1.1f;
        groupMoveConfig.facingMode = P3DUI::FacingMode::None;

        m_groupMoveButton = m_api->CreateElement(groupMoveConfig);
        if (m_groupMoveButton) {
            m_toolRow->AddChild(m_groupMoveButton);
        }

        // Snap to Grid toggle button
        bool snapToGridEnabled = Grab::RemoteGrabController::IsSnapToGridEnabled();
        P3DUI::ElementConfig snapToGridConfig = P3DUI::ElementConfig::Default("tool_snap_to_grid");
        snapToGridConfig.texturePath = snapToGridEnabled
            ? "textures\\VREditor\\snap-to-grid_highlight.dds"
            : "textures\\VREditor\\snap-to-grid.dds";
        snapToGridConfig.tooltip = L"Snap to grid";
        snapToGridConfig.scale = 1.1f;
        snapToGridConfig.facingMode = P3DUI::FacingMode::None;

        m_snapToGridButton = m_api->CreateElement(snapToGridConfig);
        if (m_snapToGridButton) {
            m_toolRow->AddChild(m_snapToGridButton);
        }

        // Grid Align button (only visible when snap-to-grid is enabled)
        // Button states:
        // - Grid disabled: Hidden
        // - Grid enabled && selection != 2 && no override: Disabled icon, tooltip about selecting 2 objects
        // - Grid enabled && selection == 2 && no override: Active icon, tooltip "Align grid to selection"
        // - Grid enabled && override set: Highlight icon, tooltip "Disable grid override"
        if (snapToGridEnabled) {
            auto* selectionState = Selection::SelectionState::GetSingleton();
            size_t selCount = selectionState->GetSelectionCount();
            bool hasOverride = Grab::RemoteGrabController::GetSingleton()->GetSnapController().HasGridOverride();

            P3DUI::ElementConfig gridAlignConfig = P3DUI::ElementConfig::Default("tool_grid_align");
            gridAlignConfig.scale = 1.1f;
            gridAlignConfig.facingMode = P3DUI::FacingMode::None;

            if (hasOverride) {
                // Override is set - show highlight, clicking disables it
                gridAlignConfig.texturePath = "textures\\VREditor\\grid-reset_highlight.dds";
                gridAlignConfig.tooltip = L"Disable grid alignment";
            } else if (selCount == 2) {
                // Exactly 2 selected - can align
                gridAlignConfig.texturePath = "textures\\VREditor\\grid-reset.dds";
                gridAlignConfig.tooltip = L"Align grid to selection";
            } else {
                // Wrong selection count - show disabled
                gridAlignConfig.texturePath = "textures\\VREditor\\grid-reset-disabled.dds";
                gridAlignConfig.tooltip = L"Align grid to selection. Select exactly 2 objects (Hold A for multi select)";
            }

            m_gridAlignButton = m_api->CreateElement(gridAlignConfig);
            if (m_gridAlignButton) {
                m_toolRow->AddChild(m_gridAlignButton);
            }
        }

        spdlog::info("SelectionMenu::PopulateToolRow - Added 2 big tool buttons + {} tool buttons",
            hasGalleryItems ? 4 : 3);
    }

    P3DUI::Interface001* m_api = nullptr;
    P3DUI::Container* m_contextDependentWheel = nullptr;
    P3DUI::ScrollableContainer* m_bigToolRow = nullptr;  // ColumnGrid for prominent buttons (undo/redo)
    P3DUI::ScrollableContainer* m_toolRow = nullptr;  // ColumnGrid for tool buttons (single row)
    // Gallery-related UI elements
    P3DUI::Element* m_galleryButton = nullptr;
    P3DUI::Element* m_saveToGalleryButton = nullptr;

    // Tool row toggle buttons
    P3DUI::Element* m_groupMoveButton = nullptr;
    P3DUI::Element* m_snapToGridButton = nullptr;
    P3DUI::Element* m_gridAlignButton = nullptr;

    // Context menu state
    ContextMenuState m_contextMenuState = ContextMenuState::Hidden;

    // B button callback for quick-select
    EditModeInputManager::CallbackId m_bButtonCallbackId = EditModeInputManager::InvalidCallbackId;

    // Track which hand opened the menu (for hover detection on release)
    bool m_menuOpenedByLeftHand = false;

    bool m_initialized = false;
    bool m_menuSetup = false;

    SelectionActionCallback m_selectionActionCallback = nullptr;

    P3DUI::Root* GetRoot() const
    {
        return MenuStateManager::GetSingleton()->GetSelectionRoot();
    }
};
