#include "SaveGameDataManager.h"
#include "ChangedObjectRegistry.h"
#include "CreatedObjectTracker.h"
#include "BaseObjectSwapperExporter.h"
#include "AddedObjectsExporter.h"
#include "../config/ConfigStorage.h"
#include "../config/ConfigOptions.h"
#include "../gallery/GalleryManager.h"
#include "../log.h"
#include <algorithm>
#include <chrono>

namespace Persistence {

SaveGameDataManager* SaveGameDataManager::GetSingleton()
{
    static SaveGameDataManager instance;
    return &instance;
}

void SaveGameDataManager::Initialize(const SKSE::SerializationInterface* serialization)
{
    if (m_initialized) {
        spdlog::warn("SaveGameDataManager already initialized");
        return;
    }

    if (!serialization) {
        spdlog::error("SaveGameDataManager: SerializationInterface is null");
        return;
    }

    // SKSE returns const interface but callbacks need non-const access
    // This is safe - SKSE's design expects this usage pattern
    auto* intfc = const_cast<SKSE::SerializationInterface*>(serialization);

    intfc->SetUniqueID(kRecordType);
    intfc->SetSaveCallback(OnSave);
    intfc->SetLoadCallback(OnLoad);
    intfc->SetRevertCallback(OnRevert);

    m_initialized = true;
    spdlog::info("SaveGameDataManager initialized (record type: {:08X}, version: {})",
        kRecordType, kDataVersion);
}

void SaveGameDataManager::OnSave(SKSE::SerializationInterface* intfc)
{
    spdlog::info("SaveGameDataManager: Saving changed objects...");

    // Delete all created objects from game world before save
    // This prevents them from being saved to the normal game save file
    // We store the player's current cell to respawn objects there after save
    auto* tracker = CreatedObjectTracker::GetSingleton();
    std::string playerCellFormKey = tracker->OnPreSave();

    // Export pending changes to Base Object Swapper INI files (repositioned existing refs)
    auto* bosExporter = BaseObjectSwapperExporter::GetSingleton();
    size_t bosExportedCount = bosExporter->ExportPendingChanges();
    if (bosExportedCount > 0) {
        spdlog::info("SaveGameDataManager: Exported {} entries to BOS INI files", bosExportedCount);
    }

    // Export created objects to AddedObjects INI files (dynamically spawned refs)
    auto* addedExporter = AddedObjectsExporter::GetSingleton();
    size_t addedExportedCount = addedExporter->ExportPendingCreatedObjects();
    if (addedExportedCount > 0) {
        spdlog::info("SaveGameDataManager: Exported {} entries to AddedObjects INI files", addedExportedCount);
    }

    // NOTE: Spriggit export feature removed - using BOS + AddedObjects INI system instead

    auto* registry = ChangedObjectRegistry::GetSingleton();
    const auto& entries = registry->GetAllEntries();

    if (!intfc->OpenRecord(kRecordType, kDataVersion)) {
        spdlog::error("SaveGameDataManager: Failed to open record for writing");
        return;
    }

    // Collect entries and sort by timestamp (newest first) to enforce limit
    std::vector<std::pair<std::string, ChangedObjectRuntimeData>> sortedEntries;
    sortedEntries.reserve(entries.size());
    for (const auto& [key, data] : entries) {
        sortedEntries.emplace_back(key, data);
    }

    // Sort by timestamp descending (newest first)
    std::sort(sortedEntries.begin(), sortedEntries.end(),
        [](const auto& a, const auto& b) {
            return a.second.saveData.timestamp > b.second.saveData.timestamp;
        });

    size_t entriesToSave = sortedEntries.size();


    // Write entry count
    uint32_t count = static_cast<uint32_t>(entriesToSave);
    if (!intfc->WriteRecordData(count)) {
        spdlog::error("SaveGameDataManager: Failed to write entry count");
        return;
    }

    // Write each entry (up to limit)
    uint32_t written = 0;
    for (size_t i = 0; i < entriesToSave; i++) {
        const auto& save = sortedEntries[i].second.saveData;

        // Write form key string
        if (!WriteString(intfc, save.formKeyString)) {
            spdlog::error("SaveGameDataManager: Failed to write formKeyString for entry {}", written);
            return;
        }

        // Write transform
        if (!WriteTransform(intfc, save.originalTransform)) {
            spdlog::error("SaveGameDataManager: Failed to write transform for entry {}", written);
            return;
        }

        // Write deleted flag
        uint8_t deleted = save.wasDeleted ? 1 : 0;
        if (!intfc->WriteRecordData(deleted)) {
            spdlog::error("SaveGameDataManager: Failed to write deleted flag for entry {}", written);
            return;
        }

        // If deleted, write base form key
        if (save.wasDeleted) {
            if (!WriteString(intfc, save.baseFormKey)) {
                spdlog::error("SaveGameDataManager: Failed to write baseFormKey for entry {}", written);
                return;
            }
        }

        // Write timestamp
        if (!intfc->WriteRecordData(save.timestamp)) {
            spdlog::error("SaveGameDataManager: Failed to write timestamp for entry {}", written);
            return;
        }

        // v3: Write cell info (captured at registration time for unloaded cell support)
        if (!WriteString(intfc, save.cellFormKey)) {
            spdlog::error("SaveGameDataManager: Failed to write cellFormKey for entry {}", written);
            return;
        }
        if (!WriteString(intfc, save.cellEditorId)) {
            spdlog::error("SaveGameDataManager: Failed to write cellEditorId for entry {}", written);
            return;
        }

        written++;
    }

    spdlog::info("SaveGameDataManager: Saved {} changed object entries", written);

    // === Save Gallery Items ===
    auto* gallery = Gallery::GalleryManager::GetSingleton();
    const auto& galleryItems = gallery->GetObjects();

    if (!intfc->OpenRecord(kGalleryRecordType, kGalleryDataVersion)) {
        spdlog::error("SaveGameDataManager: Failed to open gallery record for writing");
        return;
    }

    uint32_t galleryCount = static_cast<uint32_t>(galleryItems.size());
    if (!intfc->WriteRecordData(galleryCount)) {
        spdlog::error("SaveGameDataManager: Failed to write gallery count");
        return;
    }

    uint32_t galleryWritten = 0;
    for (const auto& item : galleryItems) {
        // Write meshPath first (primary key)
        if (!WriteString(intfc, item.meshPath)) {
            spdlog::error("SaveGameDataManager: Failed to write gallery meshPath");
            return;
        }
        if (!WriteString(intfc, item.baseFormKey)) {
            spdlog::error("SaveGameDataManager: Failed to write gallery baseFormKey");
            return;
        }
        if (!WriteString(intfc, item.displayName)) {
            spdlog::error("SaveGameDataManager: Failed to write gallery displayName");
            return;
        }
        if (!intfc->WriteRecordData(item.targetScale)) {
            spdlog::error("SaveGameDataManager: Failed to write gallery targetScale");
            return;
        }
        if (!intfc->WriteRecordData(item.originalScale)) {
            spdlog::error("SaveGameDataManager: Failed to write gallery originalScale");
            return;
        }
        if (!intfc->WriteRecordData(item.addedTimestamp)) {
            spdlog::error("SaveGameDataManager: Failed to write gallery timestamp");
            return;
        }
        galleryWritten++;
    }

    spdlog::info("SaveGameDataManager: Saved {} gallery items", galleryWritten);


    // Respawn created objects in player's current cell after save completes
    // This prevents the "objects disappear" visual glitch when saving
    tracker->OnPostSave(playerCellFormKey);
}

void SaveGameDataManager::OnLoad(SKSE::SerializationInterface* intfc)
{
    spdlog::info("SaveGameDataManager: Loading changed objects...");

    // Clear existing entries before loading
    auto* registry = ChangedObjectRegistry::GetSingleton();
    registry->Clear();

    auto* gallery = Gallery::GalleryManager::GetSingleton();
    gallery->Clear();

    uint32_t type, version, length;
    std::vector<ChangedObjectSaveGameData> entries;
    std::vector<Gallery::GalleryItem> galleryItems;

    while (intfc->GetNextRecordInfo(type, version, length)) {

        // === Handle Gallery Record ===
        if (type == kGalleryRecordType) {
            uint32_t galleryCount = 0;
            if (!intfc->ReadRecordData(galleryCount)) {
                spdlog::error("SaveGameDataManager: Failed to read gallery count");
                continue;
            }

            if (galleryCount > kMaxEntryCount) {
                spdlog::error("SaveGameDataManager: Gallery count {} exceeds max {}", galleryCount, kMaxEntryCount);
                continue;
            }

            for (uint32_t i = 0; i < galleryCount; i++) {
                Gallery::GalleryItem item;
                // Read meshPath first (primary key)
                if (!ReadString(intfc, item.meshPath)) {
                    spdlog::error("SaveGameDataManager: Failed to read gallery meshPath");
                    break;
                }
                if (!ReadString(intfc, item.baseFormKey)) {
                    spdlog::error("SaveGameDataManager: Failed to read gallery baseFormKey");
                    break;
                }
                if (!ReadString(intfc, item.displayName)) {
                    spdlog::error("SaveGameDataManager: Failed to read gallery displayName");
                    break;
                }
                if (!intfc->ReadRecordData(item.targetScale)) {
                    spdlog::error("SaveGameDataManager: Failed to read gallery targetScale");
                    break;
                }
                // v3+: read originalScale, v2: migrate to 1.0
                if (version >= 3) {
                    if (!intfc->ReadRecordData(item.originalScale)) {
                        spdlog::error("SaveGameDataManager: Failed to read gallery originalScale");
                        break;
                    }
                } else {
                    item.originalScale = 1.0f;  // Migration: v2 items default to scale 1.0
                }
                if (!intfc->ReadRecordData(item.addedTimestamp)) {
                    spdlog::error("SaveGameDataManager: Failed to read gallery timestamp");
                    break;
                }
                galleryItems.push_back(std::move(item));
            }
            continue;
        }

        // === Handle Changed Objects Record ===
        if (type != kRecordType) {
            spdlog::warn("SaveGameDataManager: Unknown record type {:08X}, skipping", type);
            continue;
        }

        if (version != kDataVersion) {
            spdlog::warn("SaveGameDataManager: Unknown version {}, attempting to load anyway", version);
        }

        // Read entry count
        uint32_t count = 0;
        if (!intfc->ReadRecordData(count)) {
            spdlog::error("SaveGameDataManager: Failed to read entry count");
            return;
        }

        // Bounds check to prevent infinite loops from corrupted saves
        if (count > kMaxEntryCount) {
            spdlog::error("SaveGameDataManager: Entry count {} exceeds maximum {} (corrupted save?)",
                count, kMaxEntryCount);
            return;
        }

        spdlog::trace("SaveGameDataManager: Reading {} entries", count);

        // Read each entry
        for (uint32_t i = 0; i < count; i++) {
            ChangedObjectSaveGameData saveData;

            // Read form key string
            if (!ReadString(intfc, saveData.formKeyString)) {
                spdlog::error("SaveGameDataManager: Failed to read formKeyString for entry {}", i);
                return;
            }

            // Read transform
            if (!ReadTransform(intfc, saveData.originalTransform)) {
                spdlog::error("SaveGameDataManager: Failed to read transform for entry {}", i);
                return;
            }

            // Read deleted flag
            uint8_t deleted = 0;
            if (!intfc->ReadRecordData(deleted)) {
                spdlog::error("SaveGameDataManager: Failed to read deleted flag for entry {}", i);
                return;
            }
            saveData.wasDeleted = (deleted != 0);

            // If deleted, read base form key
            if (saveData.wasDeleted) {
                if (!ReadString(intfc, saveData.baseFormKey)) {
                    spdlog::error("SaveGameDataManager: Failed to read baseFormKey for entry {}", i);
                    return;
                }
            }

            // Read timestamp (version 2+)
            if (version >= 2) {
                if (!intfc->ReadRecordData(saveData.timestamp)) {
                    spdlog::error("SaveGameDataManager: Failed to read timestamp for entry {}", i);
                    return;
                }
            } else {
                // Legacy data without timestamp - use current time
                saveData.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            }

            // Read cell info (version 3+)
            // v2 saves don't have cell info - will be empty, triggering runtime lookup fallback
            if (version >= 3) {
                if (!ReadString(intfc, saveData.cellFormKey)) {
                    spdlog::error("SaveGameDataManager: Failed to read cellFormKey for entry {}", i);
                    return;
                }
                if (!ReadString(intfc, saveData.cellEditorId)) {
                    spdlog::error("SaveGameDataManager: Failed to read cellEditorId for entry {}", i);
                    return;
                }
            }
            // Note: for version < 3, cellFormKey and cellEditorId remain empty (default)
            // The exporters will fall back to runtime lookup for these legacy entries

            entries.push_back(std::move(saveData));
        }
    }

    // Load all entries into registry
    registry->LoadEntries(std::move(entries));

    // Load gallery entries
    gallery->LoadEntries(std::move(galleryItems));

    spdlog::info("SaveGameDataManager: Loaded {} changed object entries, {} gallery items",
        registry->Count(), gallery->GetCount());
}

void SaveGameDataManager::OnRevert(SKSE::SerializationInterface* /*intfc*/)
{
    spdlog::info("SaveGameDataManager: Reverting (clearing changed objects and gallery)...");

    auto* registry = ChangedObjectRegistry::GetSingleton();
    registry->Clear();

    auto* gallery = Gallery::GalleryManager::GetSingleton();
    gallery->Clear();

    spdlog::info("SaveGameDataManager: Revert complete");
}

bool SaveGameDataManager::WriteString(SKSE::SerializationInterface* intfc, const std::string& str)
{
    uint32_t length = static_cast<uint32_t>(str.size());
    if (!intfc->WriteRecordData(length)) {
        return false;
    }
    if (length > 0) {
        if (!intfc->WriteRecordData(str.data(), length)) {
            return false;
        }
    }
    return true;
}

bool SaveGameDataManager::ReadString(SKSE::SerializationInterface* intfc, std::string& str)
{
    uint32_t length = 0;
    if (!intfc->ReadRecordData(length)) {
        return false;
    }

    // Bounds check to prevent memory exhaustion from corrupted saves
    if (length > kMaxStringLength) {
        spdlog::error("SaveGameDataManager: String length {} exceeds maximum {} (corrupted save?)",
            length, kMaxStringLength);
        return false;
    }

    if (length > 0) {
        str.resize(length);
        if (!intfc->ReadRecordData(str.data(), length)) {
            return false;
        }
    } else {
        str.clear();
    }
    return true;
}

bool SaveGameDataManager::WriteTransform(SKSE::SerializationInterface* intfc,
                                         const RE::NiTransform& transform)
{
    // Write position (3 floats)
    if (!intfc->WriteRecordData(transform.translate.x)) return false;
    if (!intfc->WriteRecordData(transform.translate.y)) return false;
    if (!intfc->WriteRecordData(transform.translate.z)) return false;

    // Write rotation matrix (9 floats, row-major)
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (!intfc->WriteRecordData(transform.rotate.entry[i][j])) {
                return false;
            }
        }
    }

    // Write scale (1 float)
    if (!intfc->WriteRecordData(transform.scale)) return false;

    return true;
}

bool SaveGameDataManager::ReadTransform(SKSE::SerializationInterface* intfc,
                                        RE::NiTransform& transform)
{
    // Read position (3 floats)
    if (!intfc->ReadRecordData(transform.translate.x)) return false;
    if (!intfc->ReadRecordData(transform.translate.y)) return false;
    if (!intfc->ReadRecordData(transform.translate.z)) return false;

    // Read rotation matrix (9 floats, row-major)
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (!intfc->ReadRecordData(transform.rotate.entry[i][j])) {
                return false;
            }
        }
    }

    // Read scale (1 float)
    if (!intfc->ReadRecordData(transform.scale)) return false;

    return true;
}

} // namespace Persistence
