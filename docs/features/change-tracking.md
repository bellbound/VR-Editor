# Change Tracking and INI Export Flow

This document traces how VR-Editor tracks modified and created objects and exports them to INI files for persistence and interoperability.

**Architecture Layers**
- UI and action capture: `skse/VR-Editor/src/actions/ActionHistoryRepository.cpp`, `skse/VR-Editor/src/actions/CopyHandler.h`, `skse/VR-Editor/src/actions/DeleteHandler.h`, `skse/VR-Editor/src/actions/UndoRedoController.cpp`, `skse/VR-Editor/src/ui/GalleryMenu.h`
- Persistence registry and keys: `skse/VR-Editor/src/persistence/ChangedObjectRegistry.h`, `skse/VR-Editor/src/persistence/ChangedObjectRegistry.cpp`, `skse/VR-Editor/src/persistence/FormKeyUtil.h`
- Export orchestration: `skse/VR-Editor/src/persistence/BaseObjectSwapperExporter.h`, `skse/VR-Editor/src/persistence/BaseObjectSwapperExporter.cpp`, `skse/VR-Editor/src/persistence/AddedObjectsExporter.h`, `skse/VR-Editor/src/persistence/AddedObjectsExporter.cpp`
- File IO and formats: `skse/VR-Editor/src/persistence/BaseObjectSwapperParser.h`, `skse/VR-Editor/src/persistence/BaseObjectSwapperParser.cpp`, `skse/VR-Editor/src/persistence/AddedObjectsParser.h`, `skse/VR-Editor/src/persistence/AddedObjectsParser.cpp`
- Save/load hooks: `skse/VR-Editor/src/persistence/SaveGameDataManager.h`, `skse/VR-Editor/src/persistence/SaveGameDataManager.cpp`, `skse/VR-Editor/src/plugin.cpp`
- Settings: `skse/VR-Editor/src/config/ConfigOptions.h`, `skse/VR-Editor/src/config/ConfigStorage.h`

**Core Concepts**
- All tracked objects use stable FormKey strings like `0x10C0E3~Skyrim.esm` built by `FormKeyUtil` so entries survive load-order changes.
- `ChangedObjectRegistry` stores original state for undo/persistence and current state for INI export, plus cell identity captured at registration time.
- Two export paths exist:
- Existing references (moved or deleted) go to Base Object Swapper `_SWAP` INI files.
- Newly created references (copies, gallery spawns) go to `_AddedObjects` INI files.
- Export is driven on save by `SaveGameDataManager::OnSave`, which also serializes registry data to the SKSE co-save.

**Flow: Existing References (BOS _SWAP INI)**
1. User actions that change objects are recorded in `ActionHistoryRepository` when transforms or deletes occur.
2. `ActionHistoryRepository` registers the original transform with `ChangedObjectRegistry::RegisterIfNew` or deletion with `RegisterDeletedIfNew`.
3. The same action updates the current transform for BOS via `ChangedObjectRegistry::UpdateCurrentTransform`, marking the entry as pending export.
4. Undo/redo updates current transforms via `ActionHistoryRepository::Undo` and `ActionHistoryRepository::Redo`, and undo can remove session-only registry entries through `ChangedObjectRegistry::OnActionUndone`.
5. On save, `SaveGameDataManager::OnSave` calls `BaseObjectSwapperExporter::ExportPendingChanges`.
6. `BaseObjectSwapperExporter` groups entries by cell (using stored cell keys) and writes per-cell or consolidated output based on `Config::Options::kSavePerCell`.
7. `BaseObjectSwapperParser` writes `VREditor_*_SWAP_latest.ini` in the game `Data` folder and merges with any existing entries.
8. On the next game start, `plugin.cpp` triggers the pending-latest-file apply step during `kPostLoad` so BOS reads the updated `_SWAP.ini` files.

**Flow: Created References (_AddedObjects INI)**
1. Created objects are registered at creation time, such as duplicates in `CopyHandler` and gallery spawns in `GalleryMenu`.
2. `ChangedObjectRegistry::RegisterCreatedObject` stores base form key, cell identity, and marks pending export with the current transform.
3. On save, `SaveGameDataManager::OnSave` calls `AddedObjectsExporter::ExportPendingCreatedObjects`.
4. `AddedObjectsExporter` groups entries by cell and writes per-cell or consolidated output based on `Config::Options::kSavePerCell`.
5. `AddedObjectsParser` writes `VREditor_*_AddedObjects.ini` under `Data/SKSE/Plugins/VREditor` and merges by position to prevent duplicates.
6. After successful export, `ChangedObjectRegistry::ClearPendingExportFlagsForCreatedObjects` clears the pending flags for created entries.

**Deletion Handling (Existing vs Dynamic)**
- Plugin references that are deleted are exported to BOS with the Initially Disabled flag set in `_SWAP` entries.
- Dynamic references (copies/gallery) are marked for hard delete via `ChangedObjectRegistry::MarkPendingHardDelete` and processed after load by `ChangedObjectRegistry::ProcessPendingHardDeletes`.

**What Persists Where**
- SKSE co-save: `ChangedObjectRegistry` original-state data and flags, handled by `SaveGameDataManager`.
- BOS INI: current transforms and deletion flags for existing refs, written by `BaseObjectSwapperExporter` and `BaseObjectSwapperParser`.
- AddedObjects INI: base form and transforms for created refs, written by `AddedObjectsExporter` and `AddedObjectsParser`.
