#include "log.h"
#include "HealthCheck.h"
#include "util/InputManager.h"
#include "util/MenuChecker.h"
#include "util/SkyrimNetInterface.h"
#include "interfaces/higgsinterface001.h"
#include "interfaces/ThreeDUIInterface001.h"
#include "EditModeManager.h"
#include "EditModeInputManager.h"
#include "FrameCallbackDispatcher.h"
#include "EditModeTransitioner.h"
#include "EditModeStateManager.h"
#include "selection/SelectionState.h"
#include "selection/DelayedHighlightRefreshManager.h"
#include "grab/RemoteGrabController.h"
#include "grab/RemoteSelectionController.h"
#include "grab/SphereSelectionController.h"
#include "grab/DeferredCollisionUpdateManager.h"
#include "visuals/ObjectHighlighter.h"
#include "actions/UndoRedoController.h"
#include "ui/SelectionMenu.h"
#include "ui/GalleryMenu.h"
#include "ui/MenuStateManager.h"
#include "persistence/SaveGameDataManager.h"
#include "persistence/AddedObjectsSpawner.h"
#include "persistence/CreatedObjectTracker.h"
#include "persistence/FormKeyUtil.h"
#include "persistence/BaseObjectSwapperParser.h"
#include "config/ConfigStorage.h"
#include "config/ConfigStoragePapyrusAdapter.h"
#include "config/ConfigOptions.h"
#include "api/VREditorPapyrusAPI.h"
#include "api/VRBuilderNativePapyrusAPI.h"

// =============================================================================
// Cell Event Sink - handles cell attach/detach events
// =============================================================================
// This sink handles two responsibilities:
// 1. On detach: Exit edit mode when player leaves a cell (prevents crashes from invalid refs)
// 2. On attach: Spawn added objects when player enters a cell
class CellEventSink : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
{
public:
	static CellEventSink* GetSingleton()
	{
		static CellEventSink instance;
		return &instance;
	}

	RE::BSEventNotifyControl ProcessEvent(
		const RE::TESCellAttachDetachEvent* a_event,
		RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// Check if this event is for the player
		if (a_event->reference.get() != player) {
			return RE::BSEventNotifyControl::kContinue;
		}

		if (a_event->attached) {
			// Player attached to a cell - handle streaming
			auto* cell = player->GetParentCell();
			if (cell) {
				std::string newCellFormKey = Persistence::FormKeyUtil::BuildFormKey(cell);

				spdlog::info("CellEventSink: Player entered cell {} ({:08X})",
					cell->GetFormEditorID() ? cell->GetFormEditorID() : "unnamed",
					cell->GetFormID());

				// Update current cell
				m_currentCellFormKey = newCellFormKey;

				// NOTE: With forcePersist=true, the game handles object persistence.
				// We no longer need to delete/spawn objects on cell transitions.
				// Keeping tracker reference for potential future use.
				// auto* tracker = Persistence::CreatedObjectTracker::GetSingleton();
				(void)Persistence::CreatedObjectTracker::GetSingleton();  // Suppress unused warning
			}
		} else {
			// Player detached from a cell
			// Store the cell we're leaving for cleanup when we attach to new cell
			auto* cell = player->GetParentCell();
			if (cell) {
				m_currentCellFormKey = Persistence::FormKeyUtil::BuildFormKey(cell);
			}

			// Exit edit mode to prevent crashes from invalid refs
			if (EditModeManager::GetSingleton()->IsInEditMode()) {
				spdlog::info("CellEventSink: Player detached from cell, exiting edit mode to prevent crash");
				EditModeManager::GetSingleton()->Exit();
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}

private:
	CellEventSink() = default;
	~CellEventSink() = default;
	CellEventSink(const CellEventSink&) = delete;
	CellEventSink& operator=(const CellEventSink&) = delete;

	// Track current cell for streaming cleanup
	std::string m_currentCellFormKey;
};

// Global interface pointers - available throughout the application
// g_higgsInterface is declared extern in higgsinterface001.h
// g_p3duiInterface is internal to ThreeDUIInterface001.cpp but accessible via P3DUI::GetInterface001()

// Flag to track if input systems have been initialized
static bool g_inputSystemsInitialized = false;

// Initialize input-related systems. Called on first game load/new game.
// Deferred from DataLoaded to allow 3DUI to register its VR input callbacks first.
void InitializeInputSystems()
{
	if (g_inputSystemsInitialized) {
		return;
	}

	spdlog::info("Initializing input systems (deferred initialization)");

	// Initialize InputManager (needs OpenVR hook API)
	// This registers with SkyrimVRTools - done AFTER 3DUI so 3DUI callbacks fire first
	InputManager::GetSingleton()->Initialize();

	// Initialize EditModeInputManager (needs InputManager)
	EditModeInputManager::GetSingleton()->Initialize();

	// Initialize SelectionMenu (needs 3DUI interface and EditModeInputManager)
	SelectionMenu::GetSingleton()->Initialize();

	// Initialize GalleryMenu (needs 3DUI interface and EditModeInputManager)
	GalleryMenu::GetSingleton()->Initialize();

	// Initialize MenuStateManager to create menu roots and setup menus
	MenuStateManager::GetSingleton()->Initialize();

	// Initialize EditModeTransitioner (needs InputManager and FrameCallbackDispatcher)
	EditModeTransitioner::GetSingleton()->Initialize();

	// Initialize grab controllers
	Grab::RemoteGrabController::GetSingleton()->Initialize();
	Grab::RemoteSelectionController::GetSingleton()->Initialize();
	Grab::SphereSelectionController::GetSingleton()->Initialize();

	// Initialize EditModeStateManager - coordinates selection and placement states
	// Must be after individual controllers since it owns trigger input
	EditModeStateManager::GetSingleton()->Initialize();

	// Initialize UndoRedoController (needs EditModeInputManager and FrameCallbackDispatcher)
	Actions::UndoRedoController::GetSingleton()->Initialize();

	g_inputSystemsInitialized = true;
	spdlog::info("Input systems initialized");
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostLoad:
		spdlog::info("PostLoad");

		// Apply any pending session files BEFORE BOS loads its INI files
		// BOS locks _SWAP.ini files when reading them, so we use _session.ini files
		// during gameplay and copy them to _SWAP.ini here before BOS reads them
		Persistence::BaseObjectSwapperParser::GetSingleton()->ApplyPendingSessionFiles();
		break;

	case SKSE::MessagingInterface::kPostPostLoad:
		spdlog::info("PostPostLoad - Registering HIGGS interface");
		{
			auto* messaging = SKSE::GetMessagingInterface();

			// Register HIGGS interface (doesn't need game data, safe at PostPostLoad)
			HiggsPluginAPI::GetHiggsInterface001(messaging);
			if (g_higgsInterface) {
				spdlog::info("HIGGS interface registered! Build number: {}", g_higgsInterface->GetBuildNumber());
			} else {
				spdlog::warn("HIGGS interface not available - HIGGS may not be installed");
			}
		}
		break;

	case SKSE::MessagingInterface::kDataLoaded:
		spdlog::info("DataLoaded - Initializing managers (except input - deferred to allow 3DUI to register first)");

		// Register 3DUI interface (needs TESDataHandler, must wait for DataLoaded)
		{
			auto* p3dui = P3DUI::GetInterface001();
			if (p3dui) {
				spdlog::info("3DUI interface registered! Actual version: {}, Expected version: {}",
					p3dui->GetInterfaceVersion(), P3DUI::P3DUI_INTERFACE_VERSION);
			} else {
				spdlog::warn("3DUI interface not available - 3DUI.dll may not be installed");
			}
		}

		// Register menu event handler for input blocking during menus
		MenuChecker::RegisterEventSink();

		// Initialize EditModeManager (needs HIGGS interface)
		EditModeManager::GetSingleton()->Initialize();

		// Initialize SkyrimNetInterface (needs Papyrus VM, used for VR input blocking)
		SkyrimNetInterface::GetSingleton()->Initialize();

		// Initialize FrameCallbackDispatcher
		FrameCallbackDispatcher::GetSingleton()->Initialize();

		// Initialize DeferredCollisionUpdateManager (needs FrameCallbackDispatcher)
		// This runs even outside edit mode to complete pending collision updates
		Grab::DeferredCollisionUpdateManager::GetSingleton()->Initialize();

		// Initialize ObjectHighlighter (visual feedback for selection)
		ObjectHighlighter::Initialize();

		// Initialize SelectionState (stores what objects are selected)
		Selection::SelectionState::GetSingleton()->Initialize();

		// Initialize DelayedHighlightRefreshManager (needs FrameCallbackDispatcher)
		// Handles delayed re-application of highlights after Disable/Enable cycles
		Selection::DelayedHighlightRefreshManager::GetSingleton()->Initialize();

		// Note: SelectionMenu is initialized in InitializeInputSystems() after EditModeInputManager

		// Register cell event sink - handles cell attach/detach events
		// - On detach: exits edit mode when player changes cells
		// - On attach: spawns added objects for the new cell
		if (auto* sourceHolder = RE::ScriptEventSourceHolder::GetSingleton()) {
			sourceHolder->AddEventSink<RE::TESCellAttachDetachEvent>(CellEventSink::GetSingleton());
			spdlog::info("Registered cell attach/detach event sink");
		}

		// Initialize AddedObjectsSpawner - loads and caches all _AddedObjects.ini files
		Persistence::AddedObjectsSpawner::GetSingleton()->Initialize();

		break;

	case SKSE::MessagingInterface::kPreLoadGame:
		spdlog::info("PreLoadGame");
		// Exit edit mode before loading a game to prevent crashes from invalid references
		if (EditModeManager::GetSingleton()->IsInEditMode()) {
			spdlog::info("PreLoadGame: Exiting edit mode before game load");
			EditModeManager::GetSingleton()->Exit();
		}
		break;

	case SKSE::MessagingInterface::kPostLoadGame:
		spdlog::info("PostLoadGame");

		// Process pending hard deletes for dynamic references (copies/gallery spawns)
		// These were marked for deletion but SetDelete was deferred to this safe point
		Persistence::ChangedObjectRegistry::GetSingleton()->ProcessPendingHardDeletes();

		// Initialize input systems now - after 3DUI has registered with SkyrimVRTools
		// This ensures 3DUI input callbacks fire first and can consume events
		InitializeInputSystems();

		// Notify user if VR interactivity is unavailable due to missing dependency
		if (InputManager::GetSingleton()->IsSkyrimVRToolsMissing()) {
			RE::DebugNotification("InGamePatcher VR: SkyrimVRTools not found - VR interactions disabled");
			spdlog::warn("Displayed user notification: SkyrimVRTools missing");
		}

		// Check dependency versions and notify user if incompatible (only shows once per session)
		HealthCheck::GetSingleton()->MayShowDependenciesErrorMessage();

		break;

	case SKSE::MessagingInterface::kNewGame:
		spdlog::info("NewGame");

		// Initialize input systems now - after 3DUI has registered with SkyrimVRTools
		// This ensures 3DUI input callbacks fire first and can consume events
		InitializeInputSystems();

		// Notify user if VR interactivity is unavailable due to missing dependency
		if (InputManager::GetSingleton()->IsSkyrimVRToolsMissing()) {
			RE::DebugNotification("InGamePatcher VR: SkyrimVRTools not found - VR interactions disabled");
			spdlog::warn("Displayed user notification: SkyrimVRTools missing");
		}

		// Check dependency versions and notify user if incompatible (only shows once per session)
		HealthCheck::GetSingleton()->MayShowDependenciesErrorMessage();

		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
	SKSE::Init(skse);
	SetupLog();

	spdlog::info("InGamePatcher VR loading...");
	spdlog::info("Build timestamp: {} {}", VREDIT_BUILD_DATE, VREDIT_BUILD_TIME);
	spdlog::info("Expected 3DUI interface version: {}", P3DUI::P3DUI_INTERFACE_VERSION);

	// Install main thread hook for frame updates (must be done early)
	if (!FrameCallbackDispatcher::InstallHook()) {
		spdlog::error("Failed to install FrameCallbackDispatcher hook");
		return false;
	}

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		spdlog::error("Failed to register SKSE message listener");
		return false;
	}

	// Register serialization callbacks for save/load persistence
	auto* serialization = SKSE::GetSerializationInterface();
	if (serialization) {
		Persistence::SaveGameDataManager::GetSingleton()->Initialize(serialization);
		spdlog::info("Registered serialization callbacks for ChangedObjectRegistry");
	} else {
		spdlog::warn("Serialization interface not available - changed objects will not persist");
	}

	// Initialize config storage system
	Config::ConfigStorage::GetSingleton()->Initialize("VREditor");

	// Register all config options with their default values
	// This ensures options exist in INI on first run and defines select dropdown choices
	Config::RegisterConfigOptions();

	// Register Papyrus native functions for config storage
	auto* papyrus = SKSE::GetPapyrusInterface();
	if (papyrus) {
		papyrus->Register(Config::PapyrusAdapter::Bind);
		spdlog::info("Registered Papyrus config storage functions (VREditor_IniStorage)");

		papyrus->Register(API::VREditorPapyrus::Bind);
		spdlog::info("Registered Papyrus VREditorApi functions");

		papyrus->Register(API::VRBuilderNativePapyrus::Bind);
		spdlog::info("Registered Papyrus VRBuilderNative functions");
	} else {
		spdlog::warn("Papyrus interface not available - config native API disabled");
	}

	spdlog::info("InGamePatcher VR loaded successfully");
	return true;
}
