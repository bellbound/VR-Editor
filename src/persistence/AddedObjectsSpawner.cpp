#include "AddedObjectsSpawner.h"
#include "CreatedObjectTracker.h"
#include "FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESWorldSpace.h>
#include <cmath>

namespace Persistence {

namespace {
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
}

AddedObjectsSpawner* AddedObjectsSpawner::GetSingleton()
{
    static AddedObjectsSpawner instance;
    return &instance;
}

void AddedObjectsSpawner::Initialize()
{
    if (m_initialized) {
        spdlog::warn("AddedObjectsSpawner: Already initialized");
        return;
    }

    spdlog::info("AddedObjectsSpawner: Initializing...");

    auto* parser = AddedObjectsParser::GetSingleton();
    auto iniFiles = parser->FindAllAddedObjectsIniFiles();

    if (iniFiles.empty()) {
        spdlog::info("AddedObjectsSpawner: No _AddedObjects.ini files found");
        m_initialized = true;
        return;
    }

    std::unique_lock lock(m_mutex);

    size_t totalEntries = 0;
    for (const auto& filePath : iniFiles) {
        auto fileData = parser->ParseIniFile(filePath);

        if (fileData.cellFormKey.empty()) {
            spdlog::warn("AddedObjectsSpawner: Could not determine cell FormKey for {}", filePath.string());
            continue;
        }

        if (fileData.entries.empty()) {
            spdlog::trace("AddedObjectsSpawner: No entries in {}", filePath.string());
            continue;
        }

        // Index by cell FormKey
        m_filesByCell[fileData.cellFormKey] = std::move(fileData);
        totalEntries += m_filesByCell[fileData.cellFormKey].entries.size();

        spdlog::info("AddedObjectsSpawner: Loaded {} entries for cell {} from {}",
            m_filesByCell[fileData.cellFormKey].entries.size(),
            fileData.cellFormKey,
            filePath.filename().string());
    }

    m_initialized = true;
    spdlog::info("AddedObjectsSpawner: Initialized with {} entries across {} cells",
        totalEntries, m_filesByCell.size());
}

void AddedObjectsSpawner::ResetSpawnTracking()
{
    std::unique_lock lock(m_mutex);

    size_t count = m_spawnedThisSession.size();
    m_spawnedThisSession.clear();

    spdlog::info("AddedObjectsSpawner: Reset spawn tracking (cleared {} entries)", count);
}

size_t AddedObjectsSpawner::RemoveCellEntries(const std::string& cellFormKey)
{
    std::unique_lock lock(m_mutex);

    auto it = m_filesByCell.find(cellFormKey);
    if (it == m_filesByCell.end()) {
        return 0;
    }

    size_t removedCount = it->second.entries.size();

    // Remove spawn tracking for entries in this cell
    for (const auto& entry : it->second.entries) {
        std::string entryId = GenerateEntryId(cellFormKey, entry);
        m_spawnedThisSession.erase(entryId);
    }

    m_filesByCell.erase(it);

    if (removedCount > 0) {
        spdlog::info("AddedObjectsSpawner: Removed {} cached entries for cell {}", removedCount, cellFormKey);
    }

    return removedCount;
}

void AddedObjectsSpawner::OnCellEnter(RE::TESObjectCELL* cell)
{
    // Objects are now spawned with forcePersist=true, so the game saves and
    // restores them. We only need to spawn on first encounter, not on every
    // cell enter. Skip spawning entirely - game handles persistence.
    (void)cell;
    return;

    if (!cell) {
        return;
    }

    if (!m_initialized) {
        spdlog::warn("AddedObjectsSpawner: Not initialized, ignoring cell enter");
        return;
    }

    // Build cell FormKey
    std::string cellFormKey = FormKeyUtil::BuildFormKey(cell);
    if (cellFormKey.empty()) {
        spdlog::trace("AddedObjectsSpawner: Could not build FormKey for cell {:08X}", cell->GetFormID());
        return;
    }

    // Check if we have entries for this cell
    {
        std::shared_lock lock(m_mutex);
        if (m_filesByCell.find(cellFormKey) == m_filesByCell.end()) {
            // No entries for this cell - this is the common case, so only trace log
            spdlog::trace("AddedObjectsSpawner: No entries for cell {}", cellFormKey);
            return;
        }
    }

    // We have entries - spawn them
    spdlog::info("AddedObjectsSpawner: Player entered cell {} - checking for objects to spawn", cellFormKey);
    SpawnEntriesForCell(cellFormKey, cell);
}

void AddedObjectsSpawner::SpawnEntriesForCell(const std::string& cellFormKey, RE::TESObjectCELL* cell)
{
    std::unique_lock lock(m_mutex);

    auto it = m_filesByCell.find(cellFormKey);
    if (it == m_filesByCell.end()) {
        return;
    }

    const auto& fileData = it->second;
    size_t spawnedCount = 0;
    size_t skippedCount = 0;

    for (const auto& entry : fileData.entries) {
        std::string entryId = GenerateEntryId(cellFormKey, entry);

        // Check if already spawned this session
        if (m_spawnedThisSession.contains(entryId)) {
            skippedCount++;
            continue;
        }

        // Spawn the object
        auto* ref = SpawnObject(entry, cell);
        if (ref) {
            MarkAsSpawned(entryId);
            spawnedCount++;

            // Register with CreatedObjectTracker for runtime spawning/despawning
            RE::TESForm* baseForm = AddedObjectsParser::ResolveBaseForm(entry.baseFormString);
            if (baseForm) {
                CreatedObjectTracker::GetSingleton()->Add(ref, baseForm->GetFormID(), cellFormKey);
            }

            spdlog::info("AddedObjectsSpawner: Spawned {} at ({:.1f}, {:.1f}, {:.1f}) -> ref {:08X}",
                entry.baseFormString,
                entry.position.x, entry.position.y, entry.position.z,
                ref->GetFormID());
        } else {
            spdlog::error("AddedObjectsSpawner: Failed to spawn {} at ({:.1f}, {:.1f}, {:.1f})",
                entry.baseFormString,
                entry.position.x, entry.position.y, entry.position.z);
        }
    }

    if (spawnedCount > 0 || skippedCount > 0) {
        spdlog::info("AddedObjectsSpawner: Cell {} - spawned {}, skipped {} (already spawned)",
            cellFormKey, spawnedCount, skippedCount);
    }
}

RE::TESObjectREFR* AddedObjectsSpawner::SpawnObject(const AddedObjectEntry& entry, RE::TESObjectCELL* cell)
{
    if (!cell) {
        spdlog::error("AddedObjectsSpawner: Cannot spawn object - null cell");
        return nullptr;
    }

    // Resolve base form
    RE::TESForm* baseForm = AddedObjectsParser::ResolveBaseForm(entry.baseFormString);
    if (!baseForm) {
        spdlog::error("AddedObjectsSpawner: Could not resolve base form '{}'", entry.baseFormString);
        return nullptr;
    }

    RE::TESBoundObject* boundObj = baseForm->As<RE::TESBoundObject>();
    if (!boundObj) {
        spdlog::error("AddedObjectsSpawner: Base form '{}' is not a TESBoundObject", entry.baseFormString);
        return nullptr;
    }

    // Prepare position and rotation
    RE::NiPoint3 pos = entry.position;
    RE::NiPoint3 rotRad = {
        entry.rotation.x * DEG_TO_RAD,
        entry.rotation.y * DEG_TO_RAD,
        entry.rotation.z * DEG_TO_RAD
    };

    // Get worldspace for exterior cells
    RE::TESWorldSpace* worldSpace = nullptr;
    if (!cell->IsInteriorCell()) {
        worldSpace = cell->GetRuntimeData().worldSpace;
    }

    // Create reference at location
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        spdlog::error("AddedObjectsSpawner: TESDataHandler not available");
        return nullptr;
    }

    auto refHandle = dataHandler->CreateReferenceAtLocation(
        boundObj,
        pos,
        rotRad,
        cell,
        worldSpace,
        nullptr,  // a_alreadyCreatedRef
        nullptr,  // a_primitive
        {},       // a_linkedRoomRefHandle (empty)
        true,     // a_forcePersist - let game handle persistence
        false     // a_arg11
    );

    auto* ref = refHandle.get().get();
    if (!ref) {
        spdlog::error("AddedObjectsSpawner: CreateReferenceAtLocation returned null for '{}'",
            entry.baseFormString);
        return nullptr;
    }

    // Explicitly set position, orientation, and scale after creation
    // Don't fully trust CreateReferenceAtLocation to set these correctly
    ref->SetPosition(pos);
    ref->SetAngle(rotRad);
    if (std::abs(entry.scale - 1.0f) > 0.001f) {
        ref->SetScale(entry.scale);
    }
    ref->Update3DPosition(true);

    // Disable/Enable cycle to ensure clean state and Havok sync
    ref->Disable();
    ref->Enable(false);

    return ref;
}

std::string AddedObjectsSpawner::GenerateEntryId(const std::string& cellFormKey, const AddedObjectEntry& entry)
{
    // Combine cell + base form + position into unique key
    // Position is rounded to 2 decimal places to handle float precision issues
    return fmt::format("{}|{}|{:.2f},{:.2f},{:.2f}",
        cellFormKey,
        entry.baseFormString,
        entry.position.x, entry.position.y, entry.position.z);
}

void AddedObjectsSpawner::MarkAsSpawned(const std::string& entryId)
{
    // Note: Caller should already hold the lock
    m_spawnedThisSession.insert(entryId);
}

bool AddedObjectsSpawner::HasBeenSpawnedThisSession(const std::string& entryId) const
{
    std::shared_lock lock(m_mutex);
    return m_spawnedThisSession.contains(entryId);
}

size_t AddedObjectsSpawner::GetCachedCellCount() const
{
    std::shared_lock lock(m_mutex);
    return m_filesByCell.size();
}

size_t AddedObjectsSpawner::GetTotalEntryCount() const
{
    std::shared_lock lock(m_mutex);
    size_t total = 0;
    for (const auto& [key, data] : m_filesByCell) {
        total += data.entries.size();
    }
    return total;
}

} // namespace Persistence
