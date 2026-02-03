#include "SpriggitExporter.h"
#include "FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Persistence {

SpriggitExporter* SpriggitExporter::GetSingleton()
{
    static SpriggitExporter instance;
    return &instance;
}

size_t SpriggitExporter::ExportAllChanges()
{
    auto* registry = ChangedObjectRegistry::GetSingleton();
    const auto& allEntries = registry->GetAllEntries();

    if (allEntries.empty()) {
        spdlog::trace("SpriggitExporter: No changes to export");
        return 0;
    }

    // Group entries by cell
    auto groupedEntries = GroupEntriesByCell();

    if (groupedEntries.empty()) {
        spdlog::trace("SpriggitExporter: No valid entries to export (no cells found)");
        return 0;
    }

    size_t totalExported = 0;

    for (auto& [cellFormId, cellData] : groupedEntries) {
        if (cellData.entries.empty()) {
            continue;
        }

        // Build directory path
        auto dirPath = BuildCellDirectoryPath(cellData);

        // Create directory structure
        std::error_code ec;
        std::filesystem::create_directories(dirPath, ec);
        if (ec) {
            spdlog::error("SpriggitExporter: Failed to create directory {}: {}",
                dirPath.string(), ec.message());
            continue;
        }

        // Write YAML file
        if (WritePartialYaml(dirPath, cellData)) {
            totalExported += cellData.entries.size();
            spdlog::info("SpriggitExporter: Wrote {} entries to {}",
                cellData.entries.size(), dirPath.string());
        }
    }

    if (totalExported > 0) {
        spdlog::info("SpriggitExporter: Exported {} total entries to {} cell directories",
            totalExported, groupedEntries.size());
    }

    return totalExported;
}

bool SpriggitExporter::DeleteCellExport(RE::TESObjectCELL* cell,
                                        const std::string& cellFormKey,
                                        const std::string& cellEditorId)
{
    if (cellFormKey.empty()) {
        return false;
    }

    CellExportData cellData;
    cellData.cell = cell;
    cellData.cellFormKey = cellFormKey;
    cellData.cellEditorId = cellEditorId;

    if (cellData.cell) {
        CalculateBlockSubBlock(cellData.cell, cellData.block, cellData.subBlock);
    } else {
        auto parsed = FormKeyUtil::ParseFormKey(cellFormKey);
        if (parsed) {
            RE::FormID localFormId = parsed->localFormId & 0x00FFFFFF;
            cellData.block = static_cast<int32_t>(localFormId % 10);
            cellData.subBlock = static_cast<int32_t>((localFormId / 10) % 10);
        }
    }

    auto dirPath = BuildCellDirectoryPath(cellData);
    auto filePath = dirPath / "RecordData.partial.yaml";

    bool removed = false;
    std::error_code ec;

    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath, ec);
        if (ec) {
            spdlog::warn("SpriggitExporter: Failed to delete {}: {}", filePath.string(), ec.message());
        } else {
            removed = true;
        }
    }

    auto cleanupIfEmpty = [&removed](const std::filesystem::path& path) {
        std::error_code cleanupEc;
        if (std::filesystem::exists(path) &&
            std::filesystem::is_empty(path, cleanupEc) &&
            !cleanupEc) {
            std::filesystem::remove(path, cleanupEc);
            if (!cleanupEc) {
                removed = true;
            }
        }
    };

    cleanupIfEmpty(dirPath);
    cleanupIfEmpty(dirPath.parent_path());         // subBlock
    cleanupIfEmpty(dirPath.parent_path().parent_path());  // block

    return removed;
}

std::unordered_map<RE::FormID, SpriggitExporter::CellExportData> SpriggitExporter::GroupEntriesByCell()
{
    std::unordered_map<RE::FormID, CellExportData> grouped;
    // Secondary map to look up by cellFormKey string
    std::unordered_map<std::string, RE::FormID> cellKeyToFormId;

    auto* registry = ChangedObjectRegistry::GetSingleton();
    const auto& allEntries = registry->GetAllEntries();

    for (const auto& [formKey, data] : allEntries) {
        // Use stored cell info from the registry (captured at registration time)
        const std::string& cellFormKey = data.saveData.cellFormKey;

        if (cellFormKey.empty()) {
            // No stored cell info - skip this entry
            // This should only happen for corrupted data or very old saves
            spdlog::trace("SpriggitExporter: Skipping {} - no stored cell info", formKey);
            continue;
        }

        // Check if we already have this cell in our map
        auto keyIt = cellKeyToFormId.find(cellFormKey);
        RE::FormID cellFormId;

        if (keyIt != cellKeyToFormId.end()) {
            cellFormId = keyIt->second;
        } else {
            // Resolve cell FormKey to runtime FormID
            cellFormId = FormKeyUtil::ResolveToRuntimeFormID(cellFormKey);
            if (cellFormId == 0) {
                spdlog::trace("SpriggitExporter: Could not resolve cell {} to runtime FormID", cellFormKey);
                continue;
            }

            // Initialize cell data from stored info
            CellExportData cellData;
            cellData.cellFormKey = cellFormKey;
            cellData.cellEditorId = data.saveData.cellEditorId;

            // Try to get the cell pointer if it's loaded (for CalculateBlockSubBlock)
            auto* cellForm = RE::TESForm::LookupByID(cellFormId);
            cellData.cell = cellForm ? cellForm->As<RE::TESObjectCELL>() : nullptr;

            if (cellData.cell) {
                CalculateBlockSubBlock(cellData.cell, cellData.block, cellData.subBlock);
            } else {
                // Cell not loaded - calculate block/subblock from FormID
                // For interior cells, use FormID-based calculation
                auto parsed = FormKeyUtil::ParseFormKey(cellFormKey);
                if (parsed) {
                    RE::FormID localFormId = parsed->localFormId & 0x00FFFFFF;
                    cellData.block = static_cast<int32_t>(localFormId % 10);
                    cellData.subBlock = static_cast<int32_t>((localFormId / 10) % 10);
                }
            }

            grouped[cellFormId] = std::move(cellData);
            cellKeyToFormId[cellFormKey] = cellFormId;
        }

        // Add entry to cell's list
        grouped[cellFormId].entries.emplace_back(formKey, &data);
    }

    return grouped;
}

void SpriggitExporter::CalculateBlockSubBlock(RE::TESObjectCELL* cell, int32_t& block, int32_t& subBlock)
{
    if (!cell) {
        block = 0;
        subBlock = 0;
        return;
    }

    if (cell->IsInteriorCell()) {
        // For interior cells, Block and SubBlock are calculated from FormID
        // This matches how Mutagen/Spriggit organizes interior cells
        RE::FormID formId = cell->GetFormID();
        RE::FormID localFormId = formId & 0x00FFFFFF;  // Strip load order byte

        // Simple hash distribution based on FormID
        block = static_cast<int32_t>(localFormId % 10);
        subBlock = static_cast<int32_t>((localFormId / 10) % 10);
    } else {
        // For exterior cells, use grid coordinates
        auto* exteriorData = cell->GetCoordinates();
        if (exteriorData) {
            // Exterior cells are organized by grid position
            // Block and SubBlock are derived from X,Y coordinates
            int32_t cellX = exteriorData->cellX;
            int32_t cellY = exteriorData->cellY;

            // Calculate block (32x32 cell regions)
            block = ((cellX + 128) / 32) + ((cellY + 128) / 32) * 8;
            // Calculate sub-block (8x8 cell regions within a block)
            subBlock = ((cellX + 128) % 32) / 8 + (((cellY + 128) % 32) / 8) * 4;
        } else {
            block = 0;
            subBlock = 0;
        }
    }
}

std::filesystem::path SpriggitExporter::BuildCellDirectoryPath(const CellExportData& cellData) const
{
    auto basePath = GetVREditorFolderPath() / "spriggit-partials" / "Cells";

    // Add Block/SubBlock directories
    basePath /= std::to_string(cellData.block);
    basePath /= std::to_string(cellData.subBlock);

    // Build cell directory name: "{EditorId} - {FormId}_{Plugin}/"
    // e.g., "WhiterunJorrvaskr - 0165B5_Skyrim.esm"
    std::string cellDirName;

    // Parse the FormKey to get FormID and plugin
    auto parsed = FormKeyUtil::ParseFormKey(cellData.cellFormKey);
    if (parsed) {
        std::ostringstream ss;

        // Add EditorId if available
        if (!cellData.cellEditorId.empty()) {
            ss << SanitizeForDirName(cellData.cellEditorId) << " - ";
        }

        // Add FormID (without 0x prefix)
        ss << std::uppercase << std::hex << std::setfill('0') << std::setw(6)
           << (parsed->localFormId & 0x00FFFFFF);

        // Add plugin name
        ss << "_" << SanitizeForDirName(parsed->pluginName);

        cellDirName = ss.str();
    } else {
        // Fallback if parsing fails
        cellDirName = "Unknown_" + std::to_string(cellData.cell ? cellData.cell->GetFormID() : 0);
    }

    basePath /= cellDirName;
    return basePath;
}

std::filesystem::path SpriggitExporter::GetVREditorFolderPath() const
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::filesystem::path skyrimPath(exePath);
    skyrimPath = skyrimPath.parent_path();

    return skyrimPath / "Data" / "VREditor";
}

bool SpriggitExporter::WritePartialYaml(const std::filesystem::path& dirPath,
                                         const CellExportData& cellData) const
{
    auto filePath = dirPath / "RecordData.partial.yaml";

    std::ofstream file(filePath, std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("SpriggitExporter: Failed to open {} for writing", filePath.string());
        return false;
    }

    // Write header comment
    file << "# VR Editor Spriggit Partial Export\n";
    file << "# Cell: " << (cellData.cellEditorId.empty() ? cellData.cellFormKey : cellData.cellEditorId) << "\n";
    file << "# This file contains partial data to merge into an ESP using Spriggit\n";
    file << "# FormKey: field is set to the reference ID for edited objects,\n";
    file << "# or left empty for newly added objects (Spriggit will assign new IDs)\n";
    file << "#\n";
    file << "# To use: Merge this into your mod's Cells/{Block}/{SubBlock}/{Cell}/RecordData.yaml\n";
    file << "\n";

    // Write Temporary section (where placed objects go)
    file << "Temporary:\n";

    for (const auto& [formKey, data] : cellData.entries) {
        std::string yamlEntry;

        if (data->saveData.wasCreated) {
            // New object
            yamlEntry = GenerateAddedEntryYaml(formKey, data);
        } else if (data->saveData.wasDeleted) {
            // Deleted object
            yamlEntry = GenerateDeletedEntryYaml(formKey, data);
        } else {
            // Moved/modified object
            yamlEntry = GenerateMovedEntryYaml(formKey, data);
        }

        if (!yamlEntry.empty()) {
            file << yamlEntry;
        }
    }

    file.flush();
    if (file.fail()) {
        spdlog::error("SpriggitExporter: Failed to write data to {}", filePath.string());
        return false;
    }

    return true;
}

std::string SpriggitExporter::GenerateMovedEntryYaml(const std::string& formKey,
                                                      const ChangedObjectRuntimeData* data) const
{
    std::ostringstream yaml;

    // Try to get live data from reference if loaded, otherwise use stored data
    RE::FormID runtimeFormId = FormKeyUtil::ResolveToRuntimeFormID(formKey);
    auto* form = runtimeFormId != 0 ? RE::TESForm::LookupByID(runtimeFormId) : nullptr;
    auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;

    // Get base form key - prefer stored, fallback to live lookup
    std::string baseFormKey = data->saveData.baseFormKey;
    if (baseFormKey.empty() && ref) {
        auto* baseObj = ref->GetBaseObject();
        if (baseObj) {
            baseFormKey = FormKeyUtil::BuildFormKey(baseObj);
        }
    }

    yaml << "- MutagenObjectType: PlacedObject\n";
    yaml << "  FormKey: " << ToSpriggitFormKey(formKey) << "\n";

    if (!baseFormKey.empty()) {
        yaml << "  Base: " << ToSpriggitFormKey(baseFormKey) << "\n";
    }

    // Get position and rotation - prefer live data if ref is loaded, otherwise use stored
    RE::NiPoint3 pos;
    RE::NiPoint3 rot;
    float scale = 1.0f;

    if (ref) {
        // Reference is loaded - use live game data
        pos = ref->GetPosition();
        rot = ref->GetAngle();  // Already in radians
        scale = ref->GetScale();
    } else {
        // Reference not loaded - use stored transform data
        pos = data->currentTransform.translate;
        // Convert rotation matrix to euler angles
        data->currentTransform.rotate.ToEulerAnglesXYZ(rot);
        scale = data->currentTransform.scale;
        spdlog::trace("SpriggitExporter: Using stored transform for {} (reference not loaded)", formKey);
    }

    yaml << "  Placement:\n";
    yaml << "    Position: " << FormatFloat(pos.x) << ", "
         << FormatFloat(pos.y) << ", " << FormatFloat(pos.z) << "\n";
    yaml << "    Rotation: " << FormatFloat(rot.x) << ", "
         << FormatFloat(rot.y) << ", " << FormatFloat(rot.z) << "\n";

    // Add scale if not 1.0
    if (std::abs(scale - 1.0f) > 0.001f) {
        yaml << "  Scale: " << FormatFloat(scale) << "\n";
    }

    yaml << "\n";
    return yaml.str();
}

std::string SpriggitExporter::GenerateDeletedEntryYaml(const std::string& formKey,
                                                        const ChangedObjectRuntimeData* data) const
{
    std::ostringstream yaml;

    // Get base form key from save data
    std::string baseFormKey = data->saveData.baseFormKey;

    yaml << "- MutagenObjectType: PlacedObject\n";
    yaml << "  FormKey: " << ToSpriggitFormKey(formKey) << "\n";
    yaml << "  IsDeleted: True\n";
    yaml << "  MajorRecordFlagsRaw: 32\n";
    yaml << "  SkyrimMajorRecordFlags:\n";
    yaml << "  - Deleted\n";

    if (!baseFormKey.empty()) {
        yaml << "  Base: " << ToSpriggitFormKey(baseFormKey) << "\n";
    }

    yaml << "\n";
    return yaml.str();
}

std::string SpriggitExporter::GenerateAddedEntryYaml(const std::string& formKey,
                                                      const ChangedObjectRuntimeData* data) const
{
    std::ostringstream yaml;

    // Try to get live data from reference if loaded, otherwise use stored data
    RE::FormID runtimeFormId = FormKeyUtil::ResolveToRuntimeFormID(formKey);
    auto* form = runtimeFormId != 0 ? RE::TESForm::LookupByID(runtimeFormId) : nullptr;
    auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;

    // Get base form key - prefer stored, fallback to live lookup
    std::string baseFormKey = data->saveData.baseFormKey;
    if (baseFormKey.empty() && ref) {
        auto* baseObj = ref->GetBaseObject();
        if (baseObj) {
            baseFormKey = FormKeyUtil::BuildFormKey(baseObj);
        }
    }

    // For added objects, we need at least the base form to be useful
    if (baseFormKey.empty()) {
        spdlog::warn("SpriggitExporter: Added entry {} has no base form key", formKey);
        return "";
    }

    yaml << "- MutagenObjectType: PlacedObject\n";
    yaml << "  FormKey:\n";  // Empty FormKey for new objects - Spriggit will assign

    yaml << "  Base: " << ToSpriggitFormKey(baseFormKey) << "\n";

    // Get position and rotation - prefer live data if ref is loaded, otherwise use stored
    RE::NiPoint3 pos;
    RE::NiPoint3 rot;
    float scale = 1.0f;

    if (ref) {
        // Reference is loaded - use live game data
        pos = ref->GetPosition();
        rot = ref->GetAngle();  // Already in radians
        scale = ref->GetScale();
    } else {
        // Reference not loaded - use stored transform data
        pos = data->currentTransform.translate;
        // Convert rotation matrix to euler angles
        data->currentTransform.rotate.ToEulerAnglesXYZ(rot);
        scale = data->currentTransform.scale;
        spdlog::trace("SpriggitExporter: Using stored transform for added entry {} (reference not loaded)", formKey);
    }

    yaml << "  Placement:\n";
    yaml << "    Position: " << FormatFloat(pos.x) << ", "
         << FormatFloat(pos.y) << ", " << FormatFloat(pos.z) << "\n";
    yaml << "    Rotation: " << FormatFloat(rot.x) << ", "
         << FormatFloat(rot.y) << ", " << FormatFloat(rot.z) << "\n";

    // Add scale if not 1.0
    if (std::abs(scale - 1.0f) > 0.001f) {
        yaml << "  Scale: " << FormatFloat(scale) << "\n";
    }

    yaml << "\n";
    return yaml.str();
}

std::string SpriggitExporter::ToSpriggitFormKey(const std::string& formKey)
{
    // Convert "0x10C0E3~Skyrim.esm" to "10C0E3:Skyrim.esm"
    auto parsed = FormKeyUtil::ParseFormKey(formKey);
    if (!parsed) {
        return formKey;  // Return as-is if parsing fails
    }

    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0') << std::setw(6)
       << (parsed->localFormId & 0x00FFFFFF);
    ss << ":" << parsed->pluginName;

    return ss.str();
}

std::string SpriggitExporter::FormatFloat(float value)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6) << value;
    std::string result = ss.str();

    // Remove trailing zeros after decimal point, but keep at least one decimal
    size_t dotPos = result.find('.');
    if (dotPos != std::string::npos) {
        size_t lastNonZero = result.find_last_not_of('0');
        if (lastNonZero > dotPos) {
            result.erase(lastNonZero + 1);
        } else if (lastNonZero == dotPos) {
            result.erase(dotPos + 2);  // Keep "x.0"
        }
    }

    return result;
}

std::string SpriggitExporter::SanitizeForDirName(const std::string& input)
{
    std::string result;
    result.reserve(input.size());

    for (char c : input) {
        // Replace invalid directory name characters
        if (c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            result += '_';
        } else if (c == '~') {
            result += '_';  // Replace tilde (used in FormKey format)
        } else {
            result += c;
        }
    }

    return result;
}

} // namespace Persistence
