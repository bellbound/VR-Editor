#include "SystemInitializer.h"
#include "log.h"
#include "util/InputManager.h"
#include "EditModeInputManager.h"
#include "EditModeTransitioner.h"
#include "EditModeStateManager.h"
#include "grab/RemoteGrabController.h"
#include "grab/RemoteSelectionController.h"
#include "grab/SphereSelectionController.h"
#include "ui/SelectionMenu.h"
#include "ui/GalleryMenu.h"
#include "ui/MenuStateManager.h"
#include "actions/UndoRedoController.h"
#include "actions/ActionHistoryRepository.h"
#include "persistence/ChangedObjectRegistry.h"

SystemInitializer* SystemInitializer::GetSingleton()
{
    static SystemInitializer instance;
    return &instance;
}

void SystemInitializer::InitializeCoreSystems()
{
    if (m_CoreInitialized) {
        return;
    }

    spdlog::info("SystemInitializer: Initializing core systems");

    // Create ChangedObjectRegistry - tracks modified objects for persistence
    m_changedObjectRegistry = std::make_unique<Persistence::ChangedObjectRegistry>();

    m_CoreInitialized = true;
    spdlog::info("SystemInitializer: Core systems initialized");
}

Persistence::ChangedObjectRegistry& SystemInitializer::GetChangedObjectRegistry()
{
    if (!m_changedObjectRegistry) {
        spdlog::critical("SystemInitializer: GetChangedObjectRegistry called before InitializeCoreSystems!");
        throw std::runtime_error("ChangedObjectRegistry not initialized");
    }
    return *m_changedObjectRegistry;
}

void SystemInitializer::InitializeSystems_PostLoadGameOrNewGame()
{
    if (m_PostLoadGameInitialized) {
        return;
    }

    spdlog::info("SystemInitializer: Initializing PostLoadGame systems");

    // ==========================================================================
    // Phase 1: Essential systems for edit mode detection
    // These must be initialized before the player can trigger edit mode entry
    // ==========================================================================

    // Initialize InputManager (VR input hooks via SkyrimVRTools)
    // This registers with SkyrimVRTools - done AFTER 3DUI so 3DUI callbacks fire first
    InputManager::GetSingleton()->Initialize();

    // Initialize EditModeInputManager (needs InputManager)
    // Provides input abstraction layer for edit mode functionality
    EditModeInputManager::GetSingleton()->Initialize();

    // Initialize EditModeTransitioner (needs InputManager and FrameCallbackDispatcher)
    // This detects the double-tap gesture to enter edit mode
    EditModeTransitioner::GetSingleton()->Initialize();

    m_PostLoadGameInitialized = true;
    spdlog::info("SystemInitializer: PostLoadGame systems initialized");
}

bool SystemInitializer::MayInitializeOnFirstUse()
{
    if (m_FirstUseInitialized) {
        return false;
    }

    InitializeSystems_OnFirstUse();
    return true;
}

void SystemInitializer::InitializeSystems_OnFirstUse()
{
    spdlog::info("SystemInitializer: Initializing OnFirstUse systems (edit mode first entry)");

    // ==========================================================================
    // Phase 2: On-demand systems - only needed when actually using edit mode
    // Deferred initialization minimizes crash risk when mod is not actively used
    // ==========================================================================

    // Initialize SelectionMenu (needs 3DUI interface and EditModeInputManager)
    SelectionMenu::GetSingleton()->Initialize();

    // Initialize GalleryMenu (needs 3DUI interface and EditModeInputManager)
    GalleryMenu::GetSingleton()->Initialize();

    // Initialize MenuStateManager to create menu roots and setup menus
    MenuStateManager::GetSingleton()->Initialize();

    // Initialize grab controllers
    Grab::RemoteGrabController::GetSingleton()->Initialize();
    Grab::RemoteSelectionController::GetSingleton()->Initialize();
    Grab::SphereSelectionController::GetSingleton()->Initialize();

    // Initialize EditModeStateManager - coordinates selection and placement states
    // Must be after individual controllers since it owns trigger input
    EditModeStateManager::GetSingleton()->Initialize();

    // Initialize ActionHistoryRepository (needs ChangedObjectRegistry)
    Actions::ActionHistoryRepository::GetSingleton()->Initialize(*m_changedObjectRegistry);

    // Initialize UndoRedoController (needs EditModeInputManager, FrameCallbackDispatcher, and ChangedObjectRegistry)
    Actions::UndoRedoController::GetSingleton()->Initialize(*m_changedObjectRegistry);

    m_FirstUseInitialized = true;
    spdlog::info("SystemInitializer: OnFirstUse systems initialized");
}
