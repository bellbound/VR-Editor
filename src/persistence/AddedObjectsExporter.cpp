#include "AddedObjectsExporter.h"
#include "FormKeyUtil.h"
#include "../log.h"
#include "../config/ConfigStorage.h"
#include "../config/ConfigOptions.h"
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESModel.h>
#include <RE/T/TESObjectCELL.h>
#include <cmath>

namespace Persistence {

namespace {
    constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

    // Get a usable identifier for a cell
    std::string GetCellIdentifier(RE::TESObjectCELL* cell) {
        if (!cell) return "";

        const char* editorId = cell->GetFormEditorID();
        if (editorId && editorId[0] != '\0') {
            return editorId;
        }

        return FormKeyUtil::BuildFormKey(cell);
    }
}

AddedObjectsExporter* AddedObjectsExporter::GetSingleton()
{
    static AddedObjectsExporter instance;
    return &instance;
}

size_t AddedObjectsExporter::ExportPendingCreatedObjects()
{
    auto* registry = ChangedObjectRegistry::GetSingleton();
    const auto& allEntries = registry->GetAllEntries();

    // Filter to only created objects
    std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>> createdEntries;
    for (const auto& [formKey, data] : allEntries) {
        if (data.saveData.wasCreated && data.hasPendingExportChanges) {
            createdEntries.emplace_back(formKey, &data);
        }
    }

    if (createdEntries.empty()) {
        spdlog::trace("AddedObjectsExporter: No pending created objects to export");
        return 0;
    }

    spdlog::info("AddedObjectsExporter: Exporting {} created objects", createdEntries.size());
    size_t exported = ExportEntries(createdEntries);

    // Clear pending flags for created objects after successful export
    if (exported > 0) {
        registry->ClearPendingExportFlagsForCreatedObjects();
    }

    return exported;
}

size_t AddedObjectsExporter::ExportEntries(
    const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries)
{
    if (entries.empty()) {
        return 0;
    }

    // Group entries by cell
    auto groupedEntries = GroupEntriesByCell(entries);

    auto* parser = AddedObjectsParser::GetSingleton();
    auto* config = Config::ConfigStorage::GetSingleton();
    bool perCellMode = config->GetInt(Config::Options::kSavePerCell, 0) != 0;

    size_t totalExported = 0;

    if (perCellMode) {
        // Per-cell mode: write separate files for each cell
        for (const auto& [cellFormKey, cellData] : groupedEntries) {
            const auto& [cellEditorId, addedEntries] = cellData;

            std::string iniFileName = AddedObjectsParser::BuildIniFileName(cellEditorId, cellFormKey);
            auto vrEditorPath = parser->GetVREditorFolderPath();
            auto filePath = vrEditorPath / iniFileName;

            if (parser->WriteIniFile(filePath, cellFormKey, cellEditorId, addedEntries)) {
                totalExported += addedEntries.size();
                spdlog::info("AddedObjectsExporter: Wrote {} entries to {}",
                    addedEntries.size(), iniFileName);
            } else {
                spdlog::error("AddedObjectsExporter: Failed to write {}", iniFileName);
            }
        }

        spdlog::info("AddedObjectsExporter: Exported {} entries to {} INI files (per-cell mode)",
            totalExported, groupedEntries.size());
    } else {
        // Single-file mode: write all cells to one consolidated file
        std::vector<AddedObjectsCellSection> cellSections;

        for (const auto& [cellFormKey, cellData] : groupedEntries) {
            const auto& [cellEditorId, addedEntries] = cellData;

            AddedObjectsCellSection section;
            section.cellFormKey = cellFormKey;
            section.cellEditorId = cellEditorId;
            section.entries = addedEntries;

            cellSections.push_back(std::move(section));
            totalExported += addedEntries.size();
        }

        auto vrEditorPath = parser->GetVREditorFolderPath();
        auto filePath = vrEditorPath / "VREditor_AddedObjects.ini";

        if (parser->WriteConsolidatedIniFile(filePath, cellSections)) {
            spdlog::info("AddedObjectsExporter: Exported {} entries from {} cells to consolidated file (single-file mode)",
                totalExported, cellSections.size());
        } else {
            spdlog::error("AddedObjectsExporter: Failed to write consolidated file");
            totalExported = 0;
        }
    }

    return totalExported;
}

AddedObjectEntry AddedObjectsExporter::ReferenceToEntry(RE::TESObjectREFR* ref, RE::FormID baseFormId)
{
    AddedObjectEntry entry;

    if (!ref) {
        return entry;
    }

    // Build base form string
    // Prefer base form's EditorID, fallback to FormKey
    RE::TESForm* baseForm = baseFormId != 0 ? RE::TESForm::LookupByID(baseFormId) : ref->GetBaseObject();
    if (baseForm) {
        const char* editorId = baseForm->GetFormEditorID();
        if (editorId && editorId[0] != '\0') {
            entry.baseFormString = editorId;
        } else {
            entry.baseFormString = FormKeyUtil::BuildFormKey(baseForm);
        }
    }

    // Get position from game data
    entry.position = ref->GetPosition();

    // Get rotation from game data (convert radians to degrees)
    RE::NiPoint3 angleRad = ref->GetAngle();
    entry.rotation = RE::NiPoint3(
        angleRad.x * RAD_TO_DEG,
        angleRad.y * RAD_TO_DEG,
        angleRad.z * RAD_TO_DEG
    );

    // Get scale
    entry.scale = ref->GetScale();

    // Populate metadata
    PopulateEntryMetadata(entry);

    return entry;
}

AddedObjectEntry AddedObjectsExporter::TransformToEntry(const RE::NiTransform& transform, const std::string& baseFormKey)
{
    AddedObjectEntry entry;

    // Build base form string from stored FormKey
    // Try to resolve it to get the EditorID if possible
    RE::FormID baseFormId = FormKeyUtil::ResolveToRuntimeFormID(baseFormKey);
    RE::TESForm* baseForm = baseFormId != 0 ? RE::TESForm::LookupByID(baseFormId) : nullptr;

    if (baseForm) {
        const char* editorId = baseForm->GetFormEditorID();
        if (editorId && editorId[0] != '\0') {
            entry.baseFormString = editorId;
        } else {
            entry.baseFormString = baseFormKey;
        }
    } else {
        // Base form not loaded - just use the stored FormKey
        entry.baseFormString = baseFormKey;
    }

    // Get position from stored transform
    entry.position = transform.translate;

    // Get rotation from stored transform matrix (convert to euler degrees)
    RE::NiPoint3 eulerRad;
    transform.rotate.ToEulerAnglesXYZ(eulerRad);
    entry.rotation = RE::NiPoint3(
        eulerRad.x * RAD_TO_DEG,
        eulerRad.y * RAD_TO_DEG,
        eulerRad.z * RAD_TO_DEG
    );

    // Get scale from stored transform
    entry.scale = transform.scale;

    // Populate metadata (will work if base form is still loaded)
    PopulateEntryMetadata(entry);

    return entry;
}

void AddedObjectsExporter::PopulateEntryMetadata(AddedObjectEntry& entry)
{
    // Try to resolve base form and get metadata
    RE::TESForm* baseForm = AddedObjectsParser::ResolveBaseForm(entry.baseFormString);
    if (!baseForm) {
        return;
    }

    // Get editor ID
    const char* editorId = baseForm->GetFormEditorID();
    if (editorId && editorId[0] != '\0') {
        entry.editorId = editorId;
    }

    // Get display name
    const char* fullName = baseForm->GetName();
    if (fullName && fullName[0] != '\0') {
        entry.displayName = fullName;
    }

    // Get mesh name from model
    auto* model = baseForm->As<RE::TESModel>();
    if (model) {
        const char* modelPath = model->GetModel();
        if (modelPath && modelPath[0] != '\0') {
            entry.meshName = modelPath;
        }
    }

    // Get form type as fallback identifier (useful for LIGH, etc. that don't have meshes)
    entry.formTypeName = RE::FormTypeToString(baseForm->GetFormType());

    spdlog::trace("AddedObjectsExporter: Metadata for {}: editorId='{}', name='{}', mesh='{}', formType='{}'",
        entry.baseFormString, entry.editorId, entry.displayName, entry.meshName, entry.formTypeName);
}

std::unordered_map<std::string, std::pair<std::string, std::vector<AddedObjectEntry>>>
AddedObjectsExporter::GroupEntriesByCell(
    const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries)
{
    std::unordered_map<std::string, std::pair<std::string, std::vector<AddedObjectEntry>>> grouped;
    size_t skippedNoCell = 0;

    for (const auto& [formKey, data] : entries) {
        // Only process created objects
        if (!data->saveData.wasCreated) {
            continue;
        }

        // Use stored cell info from the registry (captured at registration time)
        const std::string& cellFormKey = data->saveData.cellFormKey;
        const std::string& cellEditorId = data->saveData.cellEditorId;

        if (cellFormKey.empty()) {
            // No stored cell info - skip this entry
            spdlog::trace("AddedObjectsExporter: Skipping {} - no stored cell info", formKey);
            skippedNoCell++;
            continue;
        }

        // Convert to AddedObjectEntry using stored transform data
        AddedObjectEntry addedEntry = TransformToEntry(data->currentTransform, data->saveData.baseFormKey);

        if (addedEntry.baseFormString.empty()) {
            spdlog::warn("AddedObjectsExporter: Could not create entry for {} (no base form)", formKey);
            continue;
        }

        // Add to grouped map
        auto& cellGroup = grouped[cellFormKey];
        cellGroup.first = cellEditorId;  // Store editor ID
        cellGroup.second.push_back(std::move(addedEntry));
    }

    if (skippedNoCell > 0) {
        spdlog::trace("AddedObjectsExporter: Skipped {} entries with no stored cell info", skippedNoCell);
    }

    spdlog::trace("AddedObjectsExporter: Grouped {} entries into {} cells",
        entries.size() - skippedNoCell, grouped.size());

    return grouped;
}

} // namespace Persistence
