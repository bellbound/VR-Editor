#include "CreatedObjectTracker.h"
#include "FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESWorldSpace.h>
#include <RE/P/PlayerCharacter.h>
#include <cmath>
#include <fmt/format.h>

namespace Persistence {

namespace {
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;
}

std::string TrackedCreatedObject::GetUniqueKey() const
{
    // Position-based key with 2 decimal precision
    return fmt::format("{}|{:08X}|{:.2f},{:.2f},{:.2f}",
        cellFormKey, baseFormId,
        position.x, position.y, position.z);
}

CreatedObjectTracker* CreatedObjectTracker::GetSingleton()
{
    static CreatedObjectTracker instance;
    return &instance;
}

void CreatedObjectTracker::Add(RE::TESObjectREFR* ref, RE::FormID baseFormId, const std::string& cellFormKey)
{
    if (!ref) {
        spdlog::warn("CreatedObjectTracker::Add - null ref");
        return;
    }

    TrackedCreatedObject obj;
    obj.baseFormId = baseFormId;
    obj.cellFormKey = cellFormKey;
    obj.position = ref->GetPosition();

    // Store rotation in degrees (game stores in radians)
    RE::NiPoint3 angleRad = ref->GetAngle();
    obj.rotation = RE::NiPoint3(
        angleRad.x * RAD_TO_DEG,
        angleRad.y * RAD_TO_DEG,
        angleRad.z * RAD_TO_DEG
    );

    obj.scale = ref->GetScale();
    obj.currentRefHandle = ref->GetHandle();

    std::unique_lock lock(m_mutex);

    // Check for duplicates by position
    auto& cellObjects = m_objectsByCell[cellFormKey];
    std::string key = obj.GetUniqueKey();

    for (const auto& existing : cellObjects) {
        if (existing.GetUniqueKey() == key) {
            spdlog::trace("CreatedObjectTracker::Add - duplicate at position, updating ref");
            // Update the ref handle for existing entry
            for (auto& o : cellObjects) {
                if (o.GetUniqueKey() == key) {
                    o.currentRefHandle = ref->GetHandle();
                    break;
                }
            }
            return;
        }
    }

    cellObjects.push_back(std::move(obj));

    spdlog::info("CreatedObjectTracker::Add - tracking {:08X} (base {:08X}) in cell {} at ({:.1f}, {:.1f}, {:.1f})",
        ref->GetFormID(), baseFormId, cellFormKey, obj.position.x, obj.position.y, obj.position.z);
}

void CreatedObjectTracker::Remove(RE::TESObjectREFR* ref)
{
    if (!ref) return;

    std::unique_lock lock(m_mutex);

    auto handleToRemove = ref->GetHandle();
    for (auto& [cellKey, objects] : m_objectsByCell) {
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            if (it->currentRefHandle == handleToRemove) {
                spdlog::info("CreatedObjectTracker::Remove - removed {:08X} from cell {}",
                    ref->GetFormID(), cellKey);
                objects.erase(it);
                return;
            }
        }
    }

    spdlog::trace("CreatedObjectTracker::Remove - ref {:08X} not found in tracking", ref->GetFormID());
}

void CreatedObjectTracker::RemoveByKey(const std::string& key)
{
    std::unique_lock lock(m_mutex);

    for (auto& [cellKey, objects] : m_objectsByCell) {
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            if (it->GetUniqueKey() == key) {
                spdlog::info("CreatedObjectTracker::RemoveByKey - removed {} from cell {}", key, cellKey);
                objects.erase(it);
                return;
            }
        }
    }
}

bool CreatedObjectTracker::IsTracked(const std::string& cellFormKey, const RE::NiPoint3& position) const
{
    std::shared_lock lock(m_mutex);

    auto it = m_objectsByCell.find(cellFormKey);
    if (it == m_objectsByCell.end()) {
        return false;
    }

    // Check if position matches (with small tolerance)
    constexpr float tolerance = 1.0f;
    for (const auto& obj : it->second) {
        float dx = std::abs(obj.position.x - position.x);
        float dy = std::abs(obj.position.y - position.y);
        float dz = std::abs(obj.position.z - position.z);
        if (dx < tolerance && dy < tolerance && dz < tolerance) {
            return true;
        }
    }

    return false;
}

std::string CreatedObjectTracker::OnPreSave()
{
    // No-op: Objects are created with forcePersist=true, so the game handles
    // saving them. We don't need to delete before save anymore.
    spdlog::trace("CreatedObjectTracker::OnPreSave - game handles persistence, nothing to do");
    return "";
}

void CreatedObjectTracker::OnPostSave(const std::string& /*playerCellFormKey*/)
{
    // No-op: Objects are created with forcePersist=true, so the game handles
    // persistence. No need to respawn after save.
    spdlog::trace("CreatedObjectTracker::OnPostSave - game handles persistence, nothing to do");
}

void CreatedObjectTracker::SpawnForCell(const std::string& cellFormKey, RE::TESObjectCELL* cell)
{
    if (!cell) {
        spdlog::warn("CreatedObjectTracker::SpawnForCell - null cell");
        return;
    }

    std::unique_lock lock(m_mutex);

    auto it = m_objectsByCell.find(cellFormKey);
    if (it == m_objectsByCell.end()) {
        spdlog::trace("CreatedObjectTracker::SpawnForCell - no tracked objects for cell {}", cellFormKey);
        return;
    }

    size_t spawnedCount = 0;
    size_t skippedCount = 0;

    for (auto& obj : it->second) {
        // Skip if already has a valid ref in world
        auto existingPtr = obj.currentRefHandle.get();
        if (auto* existingRef = existingPtr.get()) {
            if (existingRef->Get3D()) {
                skippedCount++;
                continue;
            }
        }

        auto* newRef = SpawnObject(obj, cell);
        if (newRef) {
            obj.currentRefHandle = newRef->GetHandle();
            spawnedCount++;
        }
    }

    if (spawnedCount > 0 || skippedCount > 0) {
        spdlog::info("CreatedObjectTracker::SpawnForCell - cell {}: spawned {}, skipped {} (already exist)",
            cellFormKey, spawnedCount, skippedCount);
    }
}

void CreatedObjectTracker::DeleteForCell(const std::string& cellFormKey)
{
    std::unique_lock lock(m_mutex);

    auto it = m_objectsByCell.find(cellFormKey);
    if (it == m_objectsByCell.end()) {
        return;
    }

    size_t deletedCount = 0;
    for (auto& obj : it->second) {
        // Safely resolve handle - returns null if ref was deleted by engine
        auto refPtr = obj.currentRefHandle.get();
        if (auto* ref = refPtr.get()) {
            // Update stored transform before deletion
            obj.position = ref->GetPosition();
            RE::NiPoint3 angleRad = ref->GetAngle();
            obj.rotation = RE::NiPoint3(
                angleRad.x * RAD_TO_DEG,
                angleRad.y * RAD_TO_DEG,
                angleRad.z * RAD_TO_DEG
            );
            obj.scale = ref->GetScale();

            DeleteObject(obj);
            deletedCount++;
        }
    }

    if (deletedCount > 0) {
        spdlog::info("CreatedObjectTracker::DeleteForCell - deleted {} objects from cell {}", deletedCount, cellFormKey);
    }
}

size_t CreatedObjectTracker::RemoveForCell(const std::string& cellFormKey)
{
    std::unique_lock lock(m_mutex);

    auto it = m_objectsByCell.find(cellFormKey);
    if (it == m_objectsByCell.end()) {
        return 0;
    }

    size_t removedCount = it->second.size();
    for (auto& obj : it->second) {
        DeleteObject(obj);
    }

    m_objectsByCell.erase(it);

    if (removedCount > 0) {
        spdlog::info("CreatedObjectTracker::RemoveForCell - removed {} objects from cell {}", removedCount, cellFormKey);
    }

    return removedCount;
}

size_t CreatedObjectTracker::GetCount() const
{
    std::shared_lock lock(m_mutex);
    size_t total = 0;
    for (const auto& [key, objects] : m_objectsByCell) {
        total += objects.size();
    }
    return total;
}

size_t CreatedObjectTracker::GetCountForCell(const std::string& cellFormKey) const
{
    std::shared_lock lock(m_mutex);
    auto it = m_objectsByCell.find(cellFormKey);
    if (it != m_objectsByCell.end()) {
        return it->second.size();
    }
    return 0;
}

void CreatedObjectTracker::Clear()
{
    std::unique_lock lock(m_mutex);

    size_t count = 0;
    for (const auto& [key, objects] : m_objectsByCell) {
        count += objects.size();
    }

    m_objectsByCell.clear();
    spdlog::info("CreatedObjectTracker::Clear - cleared {} tracked objects", count);
}

// NOTE: This method is currently dead code. With forcePersist=true, the game
// handles object persistence and we no longer need to spawn objects manually.
// Keeping for potential future use or if we need to revert to manual spawning.
RE::TESObjectREFR* CreatedObjectTracker::SpawnObject(TrackedCreatedObject& obj, RE::TESObjectCELL* cell)
{
    if (!cell) return nullptr;

    // Resolve base form
    auto* baseForm = RE::TESForm::LookupByID(obj.baseFormId);
    if (!baseForm) {
        spdlog::error("CreatedObjectTracker::SpawnObject - could not find base form {:08X}", obj.baseFormId);
        return nullptr;
    }

    auto* boundObj = baseForm->As<RE::TESBoundObject>();
    if (!boundObj) {
        spdlog::error("CreatedObjectTracker::SpawnObject - base form {:08X} is not TESBoundObject", obj.baseFormId);
        return nullptr;
    }

    // Convert rotation to radians
    RE::NiPoint3 rotRad = {
        obj.rotation.x * DEG_TO_RAD,
        obj.rotation.y * DEG_TO_RAD,
        obj.rotation.z * DEG_TO_RAD
    };

    // Get worldspace for exterior cells
    RE::TESWorldSpace* worldSpace = nullptr;
    if (!cell->IsInteriorCell()) {
        worldSpace = cell->GetRuntimeData().worldSpace;
    }

    // Create reference
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        spdlog::error("CreatedObjectTracker::SpawnObject - TESDataHandler not available");
        return nullptr;
    }

    auto refHandle = dataHandler->CreateReferenceAtLocation(
        boundObj,
        obj.position,
        rotRad,
        cell,
        worldSpace,
        nullptr,  // a_alreadyCreatedRef
        nullptr,  // a_primitive
        {},       // a_linkedRoomRefHandle
        true,     // a_forcePersist - let game handle persistence
        false     // a_arg11
    );

    auto* ref = refHandle.get().get();
    if (!ref) {
        spdlog::error("CreatedObjectTracker::SpawnObject - CreateReferenceAtLocation failed for {:08X}", obj.baseFormId);
        return nullptr;
    }

    // Explicitly set position, orientation, and scale after creation
    // Don't fully trust CreateReferenceAtLocation to set these correctly
    ref->SetPosition(obj.position);
    ref->SetAngle(rotRad);
    if (std::abs(obj.scale - 1.0f) > 0.001f) {
        ref->SetScale(obj.scale);
    }
    ref->Update3DPosition(true);

    // Disable/Enable cycle to ensure clean state and Havok sync
    ref->Disable();
    ref->Enable(false);

    spdlog::trace("CreatedObjectTracker::SpawnObject - spawned {:08X} (base {:08X}) at ({:.1f}, {:.1f}, {:.1f})",
        ref->GetFormID(), obj.baseFormId, obj.position.x, obj.position.y, obj.position.z);

    return ref;
}

void CreatedObjectTracker::DeleteObject(TrackedCreatedObject& obj)
{
    // Safely resolve handle - returns null if ref was already deleted by engine
    auto refPtr = obj.currentRefHandle.get();
    auto* ref = refPtr.get();
    if (!ref) {
        // Handle was invalid - ref already deleted, just clear our handle
        obj.currentRefHandle.reset();
        return;
    }

    // Use SetDelete to remove from game world
    // This marks it for cleanup by the engine
    ref->Disable();
    ref->SetDelete(true);

    spdlog::trace("CreatedObjectTracker::DeleteObject - deleted {:08X}", ref->GetFormID());

    obj.currentRefHandle.reset();
}

RE::TESObjectCELL* CreatedObjectTracker::ResolveCellFromFormKey(const std::string& cellFormKey) const
{
    RE::FormID runtimeId = FormKeyUtil::ResolveToRuntimeFormID(cellFormKey);
    if (runtimeId == 0) {
        return nullptr;
    }

    auto* form = RE::TESForm::LookupByID(runtimeId);
    return form ? form->As<RE::TESObjectCELL>() : nullptr;
}

} // namespace Persistence
