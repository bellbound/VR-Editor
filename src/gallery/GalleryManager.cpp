#include "GalleryManager.h"
#include "GalleryPlacementUtil.h"
#include "../persistence/FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESDataHandler.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESModel.h>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <map>

namespace Gallery {

// Target size for gallery UI mesh previews 
constexpr float GALLERY_TARGET_SIZE = 45.0f;
// Maximum scale-up to prevent tiny objects from becoming huge
constexpr float MAX_SCALE_UP = 5.0f;

GalleryManager* GalleryManager::GetSingleton()
{
    static GalleryManager instance;
    return &instance;
}

bool GalleryManager::AddObject(RE::TESObjectREFR* ref)
{
    if (!ref) {
        spdlog::warn("GalleryManager::AddObject - ref is null");
        return false;
    }

    // Get the base object
    auto* baseObj = ref->GetBaseObject();
    if (!baseObj) {
        spdlog::warn("GalleryManager::AddObject - ref has no base object");
        return false;
    }

    // Get mesh path - this is our primary key
    auto* model = baseObj->As<RE::TESModel>();
    if (!model) {
        spdlog::warn("GalleryManager::AddObject - base object has no model");
        return false;
    }

    const char* meshPathCStr = model->GetModel();
    if (!meshPathCStr || meshPathCStr[0] == '\0') {
        spdlog::warn("GalleryManager::AddObject - mesh path is empty");
        return false;
    }

    std::string meshPath(meshPathCStr);

    // Check if already in gallery (by mesh path)
    if (IsInGalleryByMesh(meshPath)) {
        spdlog::info("GalleryManager::AddObject - mesh already in gallery: {}", meshPath);
        return false;
    }

    // Build form key for spawning later
    std::string baseFormKey = Persistence::FormKeyUtil::BuildFormKey(baseObj);
    if (baseFormKey.empty()) {
        spdlog::warn("GalleryManager::AddObject - could not build form key for base object");
        return false;
    }

    // Get display name
    std::string displayName = ExtractDisplayName(baseObj);

    // Get the reference's scale (stored as percentage, convert to float)
    float refScale = ref->GetReferenceRuntimeData().refScale / 100.0f;

    // Calculate target scale for UI preview based on bounding box
    float targetScale = CalculateTargetScale(baseObj, refScale);

    // Store the original scale so spawned objects match the saved object's size
    float originalScale = refScale;

    // Create timestamp
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Add to gallery
    m_items.emplace_back(meshPath, baseFormKey, displayName, targetScale, originalScale, timestamp);

    spdlog::info("GalleryManager::AddObject - added '{}' (mesh: {}, targetScale: {:.3f}, originalScale: {:.3f}), total items: {}",
        displayName, meshPath, targetScale, originalScale, m_items.size());

    return true;
}

bool GalleryManager::RemoveObject(const std::string& meshPath)
{
    int index = FindItemIndexByMesh(meshPath);
    if (index < 0) {
        spdlog::warn("GalleryManager::RemoveObject - not found: {}", meshPath);
        return false;
    }

    std::string name = m_items[index].displayName;
    m_items.erase(m_items.begin() + index);

    spdlog::info("GalleryManager::RemoveObject - removed '{}' ({}), total items: {}",
        name, meshPath, m_items.size());

    return true;
}

bool GalleryManager::IsInGallery(RE::TESObjectREFR* ref) const
{
    if (!ref) return false;

    std::string meshPath = GetMeshPath(ref);
    if (meshPath.empty()) return false;

    return IsInGalleryByMesh(meshPath);
}

bool GalleryManager::IsInGalleryByMesh(const std::string& meshPath) const
{
    return FindItemIndexByMesh(meshPath) >= 0;
}

std::string GalleryManager::GetMeshPath(RE::TESObjectREFR* ref) const
{
    if (!ref) return "";

    auto* baseObj = ref->GetBaseObject();
    if (!baseObj) return "";

    auto* model = baseObj->As<RE::TESModel>();
    if (!model) return "";

    const char* meshPath = model->GetModel();
    if (!meshPath || meshPath[0] == '\0') return "";

    return std::string(meshPath);
}

RE::TESObjectREFR* GalleryManager::PlaceObject(const GalleryItem& item)
{
    return GalleryPlacementUtil::PlaceInFrontOfPlayer(item);
}

void GalleryManager::Clear()
{
    size_t count = m_items.size();
    m_items.clear();
    spdlog::info("GalleryManager::Clear - cleared {} items", count);
}

void GalleryManager::LoadEntries(std::vector<GalleryItem> items)
{
    m_items = std::move(items);
    spdlog::info("GalleryManager::LoadEntries - loaded {} items", m_items.size());
}

int GalleryManager::FindItemIndexByMesh(const std::string& meshPath) const
{
    // Case-insensitive comparison for mesh paths
    for (size_t i = 0; i < m_items.size(); i++) {
        if (_stricmp(m_items[i].meshPath.c_str(), meshPath.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string GalleryManager::ExtractDisplayName(RE::TESBoundObject* baseObj) const
{
    if (!baseObj) return "Unknown Object";

    // Try to get display name from TESFullName
    auto* fullName = baseObj->As<RE::TESFullName>();
    if (fullName) {
        const char* name = fullName->GetFullName();
        if (name && name[0] != '\0') {
            return std::string(name);
        }
    }

    // Fallback: extract from mesh path
    auto* model = baseObj->As<RE::TESModel>();
    if (model) {
        const char* meshPath = model->GetModel();
        if (meshPath && meshPath[0] != '\0') {
            std::string pathStr(meshPath);
            size_t lastSlash = pathStr.find_last_of("/\\");
            std::string fileName = (lastSlash != std::string::npos)
                ? pathStr.substr(lastSlash + 1)
                : pathStr;

            // Remove extension
            size_t dotPos = fileName.find_last_of('.');
            if (dotPos != std::string::npos) {
                fileName = fileName.substr(0, dotPos);
            }

            if (!fileName.empty()) {
                return fileName;
            }
        }
    }

    return "Unknown Object";
}

float GalleryManager::CalculateTargetScale(RE::TESBoundObject* baseObj, float refScale) const
{
    if (!baseObj) return 1.0f;

    auto& bd = baseObj->boundData;

    // Calculate dimensions from bound data
    float sizeX = static_cast<float>(bd.boundMax.x - bd.boundMin.x);
    float sizeY = static_cast<float>(bd.boundMax.y - bd.boundMin.y);
    float sizeZ = static_cast<float>(bd.boundMax.z - bd.boundMin.z);

    // If all bounds are 0, return default scale
    if (sizeX == 0.0f && sizeY == 0.0f && sizeZ == 0.0f) {
        spdlog::info("GalleryManager::CalculateTargetScale - bounds are 0, using default scale");
        return 0.15f;  // Default fallback (reduced for gallery UI)
    }

    // Find largest dimension
    float largestSize = (std::max)({sizeX, sizeY, sizeZ});

    // Guard against very small values
    if (largestSize < 0.001f) {
        spdlog::info("GalleryManager::CalculateTargetScale - largest dim too small, using default");
        return 0.15f;
    }

    // The bound data represents the base object's size
    // Adjust for the reference's scale to get actual world size
    float actualSize = largestSize * refScale;

    // Calculate scale factor to fit within target size
    float scaleFactor = GALLERY_TARGET_SIZE / actualSize;

    // Cap scale-up to prevent tiny objects from becoming huge
    if (scaleFactor > MAX_SCALE_UP) {
        scaleFactor = MAX_SCALE_UP;
    }

    spdlog::info("GalleryManager::CalculateTargetScale - bounds({:.1f}, {:.1f}, {:.1f}), refScale: {:.2f}, "
        "actualSize: {:.1f}, targetScale: {:.3f}",
        sizeX, sizeY, sizeZ, refScale, actualSize, scaleFactor);

    return scaleFactor;
}

std::string GalleryManager::ExtractPluginName(const std::string& baseFormKey) const
{
    // Format: "0x[LocalFormID]~[PluginName]"
    size_t tildePos = baseFormKey.find('~');
    if (tildePos == std::string::npos || tildePos + 1 >= baseFormKey.size()) {
        return "";
    }
    return baseFormKey.substr(tildePos + 1);
}

std::vector<GalleryItem> GalleryManager::GetSortedObjects() const
{
    if (m_items.empty()) {
        return {};
    }

    // Group items by plugin and track newest timestamp per group
    std::map<std::string, std::vector<const GalleryItem*>> groups;
    std::map<std::string, uint64_t> groupNewestTimestamp;

    for (const auto& item : m_items) {
        std::string plugin = ExtractPluginName(item.baseFormKey);
        groups[plugin].push_back(&item);

        // Track the newest timestamp in each group
        auto it = groupNewestTimestamp.find(plugin);
        if (it == groupNewestTimestamp.end() || item.addedTimestamp > it->second) {
            groupNewestTimestamp[plugin] = item.addedTimestamp;
        }
    }

    // Sort items within each group by timestamp (newest first)
    for (auto& [plugin, items] : groups) {
        std::sort(items.begin(), items.end(), [](const GalleryItem* a, const GalleryItem* b) {
            return a->addedTimestamp > b->addedTimestamp;  // Descending (newest first)
        });
    }

    // Build list of groups sorted by their newest timestamp (newest first)
    std::vector<std::pair<std::string, uint64_t>> sortedGroups;
    sortedGroups.reserve(groupNewestTimestamp.size());
    for (const auto& [plugin, timestamp] : groupNewestTimestamp) {
        sortedGroups.emplace_back(plugin, timestamp);
    }
    std::sort(sortedGroups.begin(), sortedGroups.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;  // Descending (newest first)
    });

    // Build final sorted vector
    std::vector<GalleryItem> result;
    result.reserve(m_items.size());

    for (const auto& [plugin, timestamp] : sortedGroups) {
        for (const GalleryItem* item : groups[plugin]) {
            result.push_back(*item);
        }
    }

    return result;
}

} // namespace Gallery
