#include "BaseObjectSwapperExporter.h"
#include "FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESModel.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/E/ExtraTextDisplayData.h>
#include <cmath>

namespace Persistence {

BaseObjectSwapperExporter* BaseObjectSwapperExporter::GetSingleton()
{
    static BaseObjectSwapperExporter instance;
    return &instance;
}

size_t BaseObjectSwapperExporter::ExportPendingChanges()
{
    auto* registry = ChangedObjectRegistry::GetSingleton();
    auto pendingEntries = registry->GetPendingExportEntries();

    if (pendingEntries.empty()) {
        spdlog::trace("BaseObjectSwapperExporter: No pending changes to export");
        return 0;
    }

    size_t exported = ExportEntries(pendingEntries);

    // Clear pending flags on success
    if (exported > 0) {
        registry->ClearPendingExportFlags();
    }

    return exported;
}

size_t BaseObjectSwapperExporter::ExportEntries(
    const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries)
{
    if (entries.empty()) {
        return 0;
    }

    // Group entries by cell
    auto groupedEntries = GroupEntriesByCell(entries);

    auto* parser = BaseObjectSwapperParser::GetSingleton();
    size_t totalExported = 0;

    for (const auto& [cellFormKey, cellData] : groupedEntries) {
        const auto& [cellEditorId, bosEntries] = cellData;

        std::string iniFileName = BaseObjectSwapperParser::BuildIniFileName(cellEditorId, cellFormKey);
        auto dataPath = parser->GetDataFolderPath();
        auto filePath = dataPath / iniFileName;

        if (parser->WriteIniFile(filePath, bosEntries)) {
            totalExported += bosEntries.size();
            spdlog::info("BaseObjectSwapperExporter: Wrote {} entries to {}",
                bosEntries.size(), iniFileName);
        } else {
            spdlog::error("BaseObjectSwapperExporter: Failed to write {}", iniFileName);
        }
    }

    spdlog::info("BaseObjectSwapperExporter: Exported {} entries to {} INI files",
        totalExported, groupedEntries.size());

    return totalExported;
}

BOSTransformEntry BaseObjectSwapperExporter::TransformToBOSEntry(
    const std::string& formKey,
    const RE::NiTransform& transform,
    bool isDeleted)
{
    BOSTransformEntry entry;
    entry.formKeyString = formKey;
    entry.isDeleted = isDeleted;

    // Try to get position/rotation from game data (ref->data) instead of NiTransform.
    // BOS writes to ref->data.location and ref->data.angle, so we must read from
    // the same source to avoid coordinate mismatches between scene graph and game data.
    RE::FormID runtimeFormId = FormKeyUtil::ResolveToRuntimeFormID(formKey);
    auto* form = runtimeFormId != 0 ? RE::TESForm::LookupByID(runtimeFormId) : nullptr;
    auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;

    if (ref) {
        // Use game data (matches what BOS will write to)
        entry.position = ref->GetPosition();

        // Convert game data angles (radians) to degrees for INI
        // Normalize to -180 to +180 range to stay within BOS's ±360° clamp
        constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;
        RE::NiPoint3 angleRad = ref->GetAngle();
        entry.rotation = NormalizeAnglesDegrees(RE::NiPoint3(
            angleRad.x * RAD_TO_DEG,
            angleRad.y * RAD_TO_DEG,
            angleRad.z * RAD_TO_DEG
        ));

        // Use scale from game data
        entry.scale = ref->GetScale();
    } else {
        // Reference not loaded (cell unloaded) - use stored NiTransform data
        // This is expected behavior when the player has moved away from the modified area
        entry.position = transform.translate;
        entry.rotation = MatrixToEulerDegrees(transform.rotate);
        entry.scale = transform.scale;

        spdlog::trace("BaseObjectSwapperExporter: Using stored transform for {} (reference not loaded)",
            formKey);
    }

    // Populate metadata from game data (will use stored data if ref not available)
    PopulateEntryMetadata(entry);

    return entry;
}

void BaseObjectSwapperExporter::PopulateEntryMetadata(BOSTransformEntry& entry)
{
    // Note: Plugin name is extracted from formKeyString dynamically via GetPluginName()
    // No need to store it separately

    // Try to resolve to runtime FormID and get reference info
    RE::FormID runtimeFormId = FormKeyUtil::ResolveToRuntimeFormID(entry.formKeyString);
    if (runtimeFormId == 0) {
        spdlog::trace("BaseObjectSwapperExporter: Could not resolve {} to runtime FormID",
            entry.formKeyString);
        return;
    }

    // Look up the form - may not be loaded if the cell is unloaded
    auto* form = RE::TESForm::LookupByID(runtimeFormId);
    if (!form) {
        // This is expected when the reference's cell is not loaded
        spdlog::trace("BaseObjectSwapperExporter: Form {:08X} not loaded (cell unloaded), metadata will be limited",
            runtimeFormId);
        return;
    }

    // Get editor ID
    const char* editorId = form->GetFormEditorID();
    if (editorId && editorId[0] != '\0') {
        entry.editorId = editorId;
    }

    // Try to get display name if it's a reference
    auto* ref = form->As<RE::TESObjectREFR>();
    if (ref) {
        // Get the display name (respects ExtraTextDisplayData)
        const char* displayName = ref->GetDisplayFullName();
        if (displayName && displayName[0] != '\0') {
            entry.displayName = displayName;
        }

        // Get base object for additional metadata
        auto* baseObj = ref->GetBaseObject();
        if (baseObj) {
            // If no editor ID on reference, try to get it from base object
            if (entry.editorId.empty()) {
                const char* baseEditorId = baseObj->GetFormEditorID();
                if (baseEditorId && baseEditorId[0] != '\0') {
                    entry.editorId = baseEditorId;
                }
            }

            // Get mesh name from base object's model
            auto* model = baseObj->As<RE::TESModel>();
            if (model) {
                const char* modelPath = model->GetModel();
                if (modelPath && modelPath[0] != '\0') {
                    entry.meshName = modelPath;
                }
            }

            // Get form type as fallback identifier (useful for LIGH, etc. that don't have meshes)
            entry.formTypeName = RE::FormTypeToString(baseObj->GetFormType());
        }
    }

    spdlog::trace("BaseObjectSwapperExporter: Metadata for {}: editorId='{}', name='{}', mesh='{}', formType='{}', plugin='{}'",
        entry.formKeyString, entry.editorId, entry.displayName, entry.meshName, entry.formTypeName, entry.GetPluginName());
}

RE::NiPoint3 BaseObjectSwapperExporter::MatrixToEulerDegrees(const RE::NiMatrix3& matrix)
{
    // Use CommonLib's built-in conversion method
    RE::NiPoint3 eulerRadians;
    matrix.ToEulerAnglesXYZ(eulerRadians);

    // Convert radians to degrees
    constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

    return NormalizeAnglesDegrees(RE::NiPoint3(
        eulerRadians.x * RAD_TO_DEG,
        eulerRadians.y * RAD_TO_DEG,
        eulerRadians.z * RAD_TO_DEG
    ));
}

float BaseObjectSwapperExporter::NormalizeAngleDegrees(float angle)
{
    // Wrap angle to -180 to +180 range
    // BOS clamps to ±360°, but normalized angles are more predictable
    angle = std::fmod(angle, 360.0f);
    if (angle > 180.0f) {
        angle -= 360.0f;
    } else if (angle < -180.0f) {
        angle += 360.0f;
    }
    return angle;
}

RE::NiPoint3 BaseObjectSwapperExporter::NormalizeAnglesDegrees(const RE::NiPoint3& angles)
{
    return RE::NiPoint3(
        NormalizeAngleDegrees(angles.x),
        NormalizeAngleDegrees(angles.y),
        NormalizeAngleDegrees(angles.z)
    );
}

std::unordered_map<std::string, std::pair<std::string, std::vector<BOSTransformEntry>>>
BaseObjectSwapperExporter::GroupEntriesByCell(
    const std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>>& entries)
{
    std::unordered_map<std::string, std::pair<std::string, std::vector<BOSTransformEntry>>> grouped;
    size_t skippedCreated = 0;
    size_t skippedNoCell = 0;

    for (const auto& [formKey, data] : entries) {
        // Skip objects that were created by this mod (e.g., via copy/duplicate)
        // BOS is for modifying existing world objects, not for spawning new ones
        if (data->saveData.wasCreated) {
            skippedCreated++;
            spdlog::trace("BaseObjectSwapperExporter: Skipping created object {} (not suitable for BOS)",
                formKey);
            continue;
        }

        // Use stored cell info from the registry (captured at registration time)
        const std::string& cellFormKey = data->saveData.cellFormKey;
        const std::string& cellEditorId = data->saveData.cellEditorId;

        if (cellFormKey.empty()) {
            // No stored cell info - skip this entry
            spdlog::trace("BaseObjectSwapperExporter: Skipping {} - no stored cell info", formKey);
            skippedNoCell++;
            continue;
        }

        // Convert to BOS entry, passing the deleted flag from save data
        BOSTransformEntry bosEntry = TransformToBOSEntry(
            formKey,
            data->currentTransform,
            data->saveData.wasDeleted
        );

        // Add to grouped map
        auto& cellGroup = grouped[cellFormKey];
        cellGroup.first = cellEditorId;  // Store editor ID
        cellGroup.second.push_back(std::move(bosEntry));
    }

    if (skippedCreated > 0) {
        spdlog::info("BaseObjectSwapperExporter: Skipped {} created objects (not exportable to BOS)",
            skippedCreated);
    }

    if (skippedNoCell > 0) {
        spdlog::trace("BaseObjectSwapperExporter: Skipped {} entries with no stored cell info",
            skippedNoCell);
    }

    spdlog::trace("BaseObjectSwapperExporter: Grouped {} entries into {} cells",
        entries.size() - skippedCreated - skippedNoCell, grouped.size());

    return grouped;
}

} // namespace Persistence
