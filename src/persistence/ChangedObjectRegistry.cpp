#include "ChangedObjectRegistry.h"
#include "FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESObjectCELL.h>

namespace Persistence {

ChangedObjectRegistry* ChangedObjectRegistry::GetSingleton()
{
    static ChangedObjectRegistry instance;
    return &instance;
}

void ChangedObjectRegistry::RegisterIfNew(RE::TESObjectREFR* ref,
                                          const RE::NiTransform& originalTransform,
                                          const Util::ActionId& actionId)
{
    if (!ref) {
        return;
    }

    std::string formKey = FormKeyUtil::BuildFormKey(ref);
    if (formKey.empty()) {
        spdlog::warn("ChangedObjectRegistry: Could not build form key for {:08X} (dynamic form?)",
            ref->GetFormID());
        return;
    }

    std::unique_lock lock(m_mutex);

    // Only register if not already present
    if (m_entries.contains(formKey)) {
        spdlog::trace("ChangedObjectRegistry: {} already registered, skipping", formKey);
        return;
    }

    ChangedObjectRuntimeData data;
    data.saveData.formKeyString = formKey;
    data.saveData.originalTransform = originalTransform;
    data.saveData.wasDeleted = false;
    data.saveData.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    data.firstChangeActionId = actionId;
    data.createdThisSession = true;

    // Capture cell info while we have access to the loaded reference
    if (auto* cell = ref->GetParentCell()) {
        data.saveData.cellFormKey = FormKeyUtil::BuildFormKey(cell);
        if (const char* editorId = cell->GetFormEditorID(); editorId && editorId[0] != '\0') {
            data.saveData.cellEditorId = editorId;
        }
    }

    m_entries.emplace(formKey, std::move(data));

    spdlog::info("ChangedObjectRegistry: Registered {} (cell: {}, first change: action {}, timestamp: {})",
        formKey, data.saveData.cellFormKey, actionId.Value(), data.saveData.timestamp);
}

void ChangedObjectRegistry::RegisterDeletedIfNew(RE::TESObjectREFR* ref,
                                                  RE::FormID baseFormId,
                                                  const RE::NiTransform& originalTransform,
                                                  const Util::ActionId& actionId)
{
    if (!ref) {
        return;
    }

    std::string formKey = FormKeyUtil::BuildFormKey(ref);
    bool isDynamic = formKey.empty();
    if (isDynamic) {
        // Dynamic forms (copies/gallery) use FormID directly with DYNAMIC marker
        formKey = fmt::format("0x{:08X}~DYNAMIC", ref->GetFormID());
        spdlog::trace("ChangedObjectRegistry: Using dynamic form key for deleted object: {}", formKey);
    }

    // Capture cell info while we have access to the loaded reference
    std::string cellFormKey;
    std::string cellEditorId;
    if (auto* cell = ref->GetParentCell()) {
        cellFormKey = FormKeyUtil::BuildFormKey(cell);
        if (const char* editorId = cell->GetFormEditorID(); editorId && editorId[0] != '\0') {
            cellEditorId = editorId;
        }
    }

    std::unique_lock lock(m_mutex);

    // Only register if not already present
    if (m_entries.contains(formKey)) {
        // Object already tracked - just update the deleted flag
        auto& existing = m_entries[formKey];
        existing.saveData.wasDeleted = true;

        // Build base form key if we have a valid base form
        if (baseFormId != 0) {
            auto* baseForm = RE::TESForm::LookupByID(baseFormId);
            if (baseForm) {
                existing.saveData.baseFormKey = FormKeyUtil::BuildFormKey(baseForm);
            }
        }

        // Update cell info if not already set (may have been loaded from save without it)
        if (existing.saveData.cellFormKey.empty() && !cellFormKey.empty()) {
            existing.saveData.cellFormKey = cellFormKey;
            existing.saveData.cellEditorId = cellEditorId;
        }

        spdlog::trace("ChangedObjectRegistry: {} already registered, marked as deleted", formKey);
        return;
    }

    ChangedObjectRuntimeData data;
    data.saveData.formKeyString = formKey;
    data.saveData.originalTransform = originalTransform;
    data.saveData.wasDeleted = true;
    data.saveData.cellFormKey = cellFormKey;
    data.saveData.cellEditorId = cellEditorId;
    data.saveData.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    data.firstChangeActionId = actionId;
    data.createdThisSession = true;

    // Build base form key if we have a valid base form
    if (baseFormId != 0) {
        auto* baseForm = RE::TESForm::LookupByID(baseFormId);
        if (baseForm) {
            data.saveData.baseFormKey = FormKeyUtil::BuildFormKey(baseForm);
        }
    }

    m_entries.emplace(formKey, std::move(data));

    spdlog::info("ChangedObjectRegistry: Registered deleted {} (cell: {}, base: {}, first change: action {}, timestamp: {})",
        formKey, data.saveData.cellFormKey, data.saveData.baseFormKey, actionId.Value(), data.saveData.timestamp);
}

void ChangedObjectRegistry::RegisterCreatedObject(RE::TESObjectREFR* ref,
                                                   RE::FormID baseFormId,
                                                   const RE::NiTransform& transform,
                                                   const Util::ActionId& actionId)
{
    if (!ref) {
        return;
    }

    std::string formKey = FormKeyUtil::BuildFormKey(ref);
    if (formKey.empty()) {
        // Created objects are dynamic forms - use FormID directly
        formKey = fmt::format("0x{:08X}~DYNAMIC", ref->GetFormID());
        spdlog::info("ChangedObjectRegistry: Using dynamic form key for created object: {}", formKey);
    }

    // Capture cell info while we have access to the loaded reference
    std::string cellFormKey;
    std::string cellEditorId;
    if (auto* cell = ref->GetParentCell()) {
        cellFormKey = FormKeyUtil::BuildFormKey(cell);
        if (const char* editorId = cell->GetFormEditorID(); editorId && editorId[0] != '\0') {
            cellEditorId = editorId;
        }
    }

    std::unique_lock lock(m_mutex);

    // Created objects should never already exist in registry
    if (m_entries.contains(formKey)) {
        spdlog::warn("ChangedObjectRegistry: Created object {} already registered (unexpected)", formKey);
        return;
    }

    ChangedObjectRuntimeData data;
    data.saveData.formKeyString = formKey;
    data.saveData.originalTransform = transform;
    data.saveData.wasDeleted = false;
    data.saveData.wasCreated = true;  // Mark as created by this mod
    data.saveData.cellFormKey = cellFormKey;
    data.saveData.cellEditorId = cellEditorId;
    data.saveData.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    data.firstChangeActionId = actionId;
    data.createdThisSession = true;

    // Store base form key
    if (baseFormId != 0) {
        auto* baseForm = RE::TESForm::LookupByID(baseFormId);
        if (baseForm) {
            data.saveData.baseFormKey = FormKeyUtil::BuildFormKey(baseForm);
        }
    }

    // Also set current transform for BOS export
    data.currentTransform = transform;
    data.hasPendingExportChanges = true;

    m_entries.emplace(formKey, std::move(data));

    spdlog::info("ChangedObjectRegistry: Registered created object {} (cell: {}, base: {}, action {}, timestamp: {})",
        formKey, data.saveData.cellFormKey, data.saveData.baseFormKey, actionId.Value(), data.saveData.timestamp);
}

void ChangedObjectRegistry::OnActionUndone(const Util::ActionId& undoneActionId)
{
    std::unique_lock lock(m_mutex);

    // Find entries that were created by this action AND in this session
    std::vector<std::string> toRemove;

    for (const auto& [key, data] : m_entries) {
        if (data.createdThisSession &&
            data.firstChangeActionId == undoneActionId) {
            toRemove.push_back(key);
        }
    }

    for (const auto& key : toRemove) {
        m_entries.erase(key);
        spdlog::info("ChangedObjectRegistry: Removed {} (first-change action undone)", key);
    }

    if (!toRemove.empty()) {
        spdlog::trace("ChangedObjectRegistry: Removed {} entries on undo", toRemove.size());
    }
}

void ChangedObjectRegistry::UpdateCurrentTransform(const std::string& formKey,
                                                    const RE::NiTransform& currentTransform,
                                                    std::string_view locationName)
{
    std::unique_lock lock(m_mutex);

    auto it = m_entries.find(formKey);
    if (it == m_entries.end()) {
        spdlog::warn("ChangedObjectRegistry: Cannot update transform for unregistered key: {}", formKey);
        return;
    }

    it->second.currentTransform = currentTransform;
    it->second.locationName = std::string(locationName);
    it->second.hasPendingExportChanges = true;

    spdlog::trace("ChangedObjectRegistry: Updated current transform for {} (location: {})",
        formKey, locationName);
}

std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>
ChangedObjectRegistry::GetPendingExportEntries() const
{
    std::shared_lock lock(m_mutex);

    std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>> pending;

    for (const auto& [key, data] : m_entries) {
        if (data.hasPendingExportChanges) {
            pending.emplace_back(key, &data);
        }
    }

    spdlog::trace("ChangedObjectRegistry: Found {} entries with pending export changes", pending.size());
    return pending;
}

void ChangedObjectRegistry::ClearPendingExportFlags()
{
    std::unique_lock lock(m_mutex);

    size_t cleared = 0;
    for (auto& [key, data] : m_entries) {
        // Only clear flags for non-created objects (BOS export)
        // Created objects need to keep their flag until AddedObjects export runs
        if (data.hasPendingExportChanges && !data.saveData.wasCreated) {
            data.hasPendingExportChanges = false;
            cleared++;
        }
    }

    spdlog::trace("ChangedObjectRegistry: Cleared pending export flags on {} entries (skipped created objects)", cleared);
}

void ChangedObjectRegistry::ClearPendingExportFlagsForCreatedObjects()
{
    std::unique_lock lock(m_mutex);

    size_t cleared = 0;
    for (auto& [key, data] : m_entries) {
        // Only clear flags for created objects (AddedObjects export)
        if (data.hasPendingExportChanges && data.saveData.wasCreated) {
            data.hasPendingExportChanges = false;
            cleared++;
        }
    }

    spdlog::trace("ChangedObjectRegistry: Cleared pending export flags on {} created objects", cleared);
}

std::optional<ChangedObjectSaveGameData> ChangedObjectRegistry::GetOriginalState(
    const std::string& formKey) const
{
    std::shared_lock lock(m_mutex);

    auto it = m_entries.find(formKey);
    if (it != m_entries.end()) {
        return it->second.saveData;
    }
    return std::nullopt;
}

bool ChangedObjectRegistry::Contains(const std::string& formKey) const
{
    std::shared_lock lock(m_mutex);
    return m_entries.contains(formKey);
}

size_t ChangedObjectRegistry::Count() const
{
    std::shared_lock lock(m_mutex);
    return m_entries.size();
}

std::vector<std::pair<std::string, ChangedObjectRuntimeData>>
ChangedObjectRegistry::ExtractEntriesForCell(const std::string& cellFormKey)
{
    std::vector<std::pair<std::string, ChangedObjectRuntimeData>> extracted;
    if (cellFormKey.empty()) {
        return extracted;
    }

    std::unique_lock lock(m_mutex);

    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->second.saveData.cellFormKey == cellFormKey) {
            extracted.emplace_back(it->first, std::move(it->second));
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }

    if (!extracted.empty()) {
        spdlog::info("ChangedObjectRegistry: Extracted {} entries for cell {}",
            extracted.size(), cellFormKey);
    }

    return extracted;
}

const std::unordered_map<std::string, ChangedObjectRuntimeData>&
ChangedObjectRegistry::GetAllEntries() const
{
    // Note: Caller must ensure thread safety when iterating the returned reference
    // This is used by SaveGameDataManager during save, which runs on a single thread
    std::shared_lock lock(m_mutex);
    return m_entries;
}

void ChangedObjectRegistry::LoadEntries(std::vector<ChangedObjectSaveGameData>&& entries)
{
    std::unique_lock lock(m_mutex);

    size_t loadedCount = entries.size();
    for (auto& saveData : entries) {
        // Loaded entries have no runtime link - they are permanent
        ChangedObjectRuntimeData data;
        data.saveData = std::move(saveData);
        data.firstChangeActionId = Util::ActionId();  // Invalid/default ID
        data.createdThisSession = false;  // Mark as loaded, not session-created

        m_entries.emplace(data.saveData.formKeyString, std::move(data));
    }

    spdlog::info("ChangedObjectRegistry: Loaded {} entries from save game", loadedCount);
}

void ChangedObjectRegistry::Clear()
{
    std::unique_lock lock(m_mutex);

    size_t count = m_entries.size();
    m_entries.clear();
    spdlog::info("ChangedObjectRegistry: Cleared {} entries", count);
}

void ChangedObjectRegistry::MarkPendingHardDelete(const std::string& formKey)
{
    std::unique_lock lock(m_mutex);

    auto it = m_entries.find(formKey);
    if (it != m_entries.end()) {
        it->second.saveData.pendingHardDelete = true;
        spdlog::info("ChangedObjectRegistry: Marked {} for hard delete on next load", formKey);
    } else {
        spdlog::warn("ChangedObjectRegistry: Cannot mark {} for hard delete - not found in registry", formKey);
    }
}

void ChangedObjectRegistry::ProcessPendingHardDeletes()
{
    std::unique_lock lock(m_mutex);

    size_t processed = 0;
    for (auto& [formKey, data] : m_entries) {
        if (!data.saveData.pendingHardDelete) {
            continue;
        }

        RE::FormID runtimeId = FormKeyUtil::ResolveToRuntimeFormID(formKey);
        if (runtimeId == 0) {
            spdlog::warn("ChangedObjectRegistry: Failed to resolve {} for hard delete", formKey);
            data.saveData.pendingHardDelete = false;
            continue;
        }

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(runtimeId);
        if (ref) {
            ref->SetDelete(true);
            spdlog::info("ChangedObjectRegistry: SetDelete on dynamic ref {:08X} ({})", runtimeId, formKey);
            processed++;
        } else {
            spdlog::warn("ChangedObjectRegistry: Could not find ref {:08X} for hard delete", runtimeId);
        }

        data.saveData.pendingHardDelete = false;
    }

    if (processed > 0) {
        spdlog::info("ChangedObjectRegistry: Processed {} pending hard deletes", processed);
    }
}

} // namespace Persistence
