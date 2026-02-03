#include "SaveFileTestManager.h"
#include "../FrameCallbackDispatcher.h"
#include "../persistence/FormKeyUtil.h"
#include "../log.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESForm.h>
#include <RE/S/ScriptEventSourceHolder.h>
#include <cmath>

namespace Test {

SaveFileTestManager* SaveFileTestManager::GetSingleton()
{
    static SaveFileTestManager instance;
    return &instance;
}

void SaveFileTestManager::Initialize()
{
    if (m_initialized) {
        return;
    }

    // Register for frame updates (not edit-mode-only since this is a test utility)
    FrameCallbackDispatcher::GetSingleton()->Register(this, false);

    // Register for cell attach/detach events
    if (auto* sourceHolder = RE::ScriptEventSourceHolder::GetSingleton()) {
        sourceHolder->AddEventSink<RE::TESCellAttachDetachEvent>(this);
        spdlog::info("SaveFileTestManager: Registered cell event sink");
    }

    m_initialized = true;
    spdlog::info("SaveFileTestManager initialized");
}


void SaveFileTestManager::StartSpawning()
{
    if (m_isSpawning) {
        spdlog::warn("SaveFileTestManager::StartSpawning - already spawning");
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("SaveFileTestManager::StartSpawning - player not available");
        return;
    }

    // Store circle center at player's current position
    m_circleCenter = player->GetPosition();
    m_objectsToSpawn = TOTAL_OBJECTS;
    m_temporarySpawned = 0;
    m_nonTemporarySpawned = 0;
    m_spawnAccumulator = 0.0f;
    m_spawnedRefs.clear();
    m_spawnedRefs.reserve(TOTAL_OBJECTS);
    m_trackedTemporaryRefId = 0;
    m_trackedNonTemporaryRefId = 0;
    m_isSpawning = true;

    spdlog::info("SaveFileTestManager::StartSpawning - spawning {} temporary + {} non-temporary at ({:.1f}, {:.1f}, {:.1f})",
        TEMPORARY_COUNT, NON_TEMPORARY_COUNT, m_circleCenter.x, m_circleCenter.y, m_circleCenter.z);

    RE::DebugNotification("SaveFileTest: Spawning 100 temp + 100 non-temp objects...");
}

void SaveFileTestManager::Reset()
{
    m_isSpawning = false;
    m_objectsToSpawn = 0;
    m_temporarySpawned = 0;
    m_nonTemporarySpawned = 0;
    m_spawnAccumulator = 0.0f;
    m_spawnedRefs.clear();
    // Don't reset tracked IDs - we want to persist those
    spdlog::info("SaveFileTestManager::Reset - cleared spawning state");
}

void SaveFileTestManager::DeleteAllDynamicRefs()
{
    // DISABLED - kept for reference
    spdlog::info("SaveFileTestManager::DeleteAllDynamicRefs - DISABLED");
}

void SaveFileTestManager::OnFrameUpdate(float deltaTime)
{
    if (!m_isSpawning || m_objectsToSpawn <= 0) {
        return;
    }

    // Accumulate time and calculate how many objects to spawn this frame
    m_spawnAccumulator += deltaTime * SPAWN_RATE;

    int objectsThisFrame = static_cast<int>(m_spawnAccumulator);
    if (objectsThisFrame <= 0) {
        return;
    }

    m_spawnAccumulator -= static_cast<float>(objectsThisFrame);

    // Clamp to remaining objects
    objectsThisFrame = (std::min)(objectsThisFrame, m_objectsToSpawn);

    for (int i = 0; i < objectsThisFrame; ++i) {
        // Calculate position on circle
        int currentIndex = TOTAL_OBJECTS - m_objectsToSpawn;
        float angle = (static_cast<float>(currentIndex) / static_cast<float>(TOTAL_OBJECTS)) * 2.0f * 3.14159265f;

        RE::NiPoint3 spawnPos = m_circleCenter;
        spawnPos.x += CIRCLE_RADIUS * std::cos(angle);
        spawnPos.y += CIRCLE_RADIUS * std::sin(angle);

        // First half are temporary, second half are non-temporary
        bool makeTemporary = (currentIndex < TEMPORARY_COUNT);

        auto* ref = SpawnAtPosition(spawnPos, makeTemporary);
        if (ref) {
            m_spawnedRefs.push_back(ref);

            if (makeTemporary) {
                m_temporarySpawned++;
                // Store first temporary as tracked ref
                if (m_trackedTemporaryRefId == 0) {
                    m_trackedTemporaryRefId = ref->GetFormID();
                    spdlog::info("SaveFileTestManager: Tracked TEMPORARY ref {:08X}", m_trackedTemporaryRefId);
                }
            } else {
                m_nonTemporarySpawned++;
                // Store first non-temporary as tracked ref
                if (m_trackedNonTemporaryRefId == 0) {
                    m_trackedNonTemporaryRefId = ref->GetFormID();
                    spdlog::info("SaveFileTestManager: Tracked NON-TEMPORARY ref {:08X}", m_trackedNonTemporaryRefId);
                }
            }
        }

        m_objectsToSpawn--;
    }

    // Check if done
    if (m_objectsToSpawn <= 0) {
        m_isSpawning = false;
        spdlog::info("SaveFileTestManager: Completed - {} temporary, {} non-temporary",
            m_temporarySpawned, m_nonTemporarySpawned);
        spdlog::info("SaveFileTestManager: Tracked refs - temp:{:08X}, non-temp:{:08X}",
            m_trackedTemporaryRefId, m_trackedNonTemporaryRefId);

        std::string msg = "SaveFileTest: Done! Temp:" + std::to_string(m_temporarySpawned) +
            " NonTemp:" + std::to_string(m_nonTemporarySpawned) + ". Save & change cell!";
        RE::DebugNotification(msg.c_str());
    }
}

RE::TESObjectREFR* SaveFileTestManager::SpawnAtPosition(const RE::NiPoint3& pos, bool makeTemporary)
{
    // Build form key and resolve to runtime FormID
    std::string formKey = Persistence::FormKeyUtil::BuildFormKey(BASE_FORM_ID, PLUGIN_NAME);
    RE::FormID runtimeFormId = Persistence::FormKeyUtil::ResolveToRuntimeFormID(formKey);

    if (runtimeFormId == 0) {
        spdlog::error("SaveFileTestManager::SpawnAtPosition - failed to resolve form key: {}", formKey);
        return nullptr;
    }

    // Look up the form
    auto* form = RE::TESForm::LookupByID(runtimeFormId);
    if (!form) {
        spdlog::error("SaveFileTestManager::SpawnAtPosition - form not found for ID {:08X}", runtimeFormId);
        return nullptr;
    }

    // Cast to bound object (required for placement)
    auto* boundObj = form->As<RE::TESBoundObject>();
    if (!boundObj) {
        spdlog::error("SaveFileTestManager::SpawnAtPosition - form is not a TESBoundObject");
        return nullptr;
    }

    // Get player for PlaceObjectAtMe
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return nullptr;
    }

    // Place the object using the same method as gallery
    auto newRefPtr = player->PlaceObjectAtMe(boundObj, false);
    RE::TESObjectREFR* newRef = newRefPtr.get();
    if (!newRef) {
        spdlog::error("SaveFileTestManager::SpawnAtPosition - PlaceObjectAtMe failed");
        return nullptr;
    }

    // Set temporary flag if requested
    if (makeTemporary) {
        newRef->formFlags |= RE::TESForm::RecordFlags::kTemporary;
    }

    // Set the position
    newRef->SetPosition(pos);
    newRef->Update3DPosition(true);

    return newRef;
}

RE::BSEventNotifyControl SaveFileTestManager::ProcessEvent(
    const RE::TESCellAttachDetachEvent* a_event,
    RE::BSTEventSource<RE::TESCellAttachDetachEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event->reference.get() != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Only check on attach (entering new cell)
    if (a_event->attached) {
        spdlog::info("SaveFileTestManager: Player attached to new cell, checking tracked refs...");
        CheckTrackedRefs();
    }

    return RE::BSEventNotifyControl::kContinue;
}

void SaveFileTestManager::CheckTrackedRefs()
{
    spdlog::info("=== SaveFileTestManager: Checking Tracked Refs ===");

    // Check temporary ref
    if (m_trackedTemporaryRefId != 0) {
        auto* tempRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(m_trackedTemporaryRefId);
        if (tempRef) {
            bool isDeleted = tempRef->IsDeleted();
            bool isDisabled = tempRef->IsDisabled();
            bool hasTemp = (tempRef->formFlags & RE::TESForm::RecordFlags::kTemporary) != 0;
            spdlog::info("  TEMPORARY ref {:08X}: FOUND (deleted={}, disabled={}, tempFlag={})",
                m_trackedTemporaryRefId, isDeleted, isDisabled, hasTemp);
            RE::DebugNotification("TempRef: FOUND - still loaded!");
        } else {
            spdlog::info("  TEMPORARY ref {:08X}: NOT FOUND (unloaded or deleted)", m_trackedTemporaryRefId);
            RE::DebugNotification("TempRef: NOT FOUND - unloaded!");
        }
    } else {
        spdlog::info("  TEMPORARY ref: No tracked ID");
    }

    // Check non-temporary ref
    if (m_trackedNonTemporaryRefId != 0) {
        auto* nonTempRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(m_trackedNonTemporaryRefId);
        if (nonTempRef) {
            bool isDeleted = nonTempRef->IsDeleted();
            bool isDisabled = nonTempRef->IsDisabled();
            bool hasTemp = (nonTempRef->formFlags & RE::TESForm::RecordFlags::kTemporary) != 0;
            spdlog::info("  NON-TEMPORARY ref {:08X}: FOUND (deleted={}, disabled={}, tempFlag={})",
                m_trackedNonTemporaryRefId, isDeleted, isDisabled, hasTemp);
            RE::DebugNotification("NonTempRef: FOUND - still loaded!");
        } else {
            spdlog::info("  NON-TEMPORARY ref {:08X}: NOT FOUND (unloaded or deleted)", m_trackedNonTemporaryRefId);
            RE::DebugNotification("NonTempRef: NOT FOUND - unloaded!");
        }
    } else {
        spdlog::info("  NON-TEMPORARY ref: No tracked ID");
    }

    spdlog::info("=== End Tracked Refs Check ===");
}

// Serialization methods - called by SaveGameDataManager
void SaveFileTestManager::SaveData(SKSE::SerializationInterface* intfc)
{
    spdlog::info("SaveFileTestManager::SaveData - saving tracked refs (temp:{:08X}, nonTemp:{:08X})",
        m_trackedTemporaryRefId, m_trackedNonTemporaryRefId);

    if (!intfc->OpenRecord(kRecordType, kDataVersion)) {
        spdlog::error("SaveFileTestManager::SaveData - failed to open record");
        return;
    }

    intfc->WriteRecordData(&m_trackedTemporaryRefId, sizeof(m_trackedTemporaryRefId));
    intfc->WriteRecordData(&m_trackedNonTemporaryRefId, sizeof(m_trackedNonTemporaryRefId));

    spdlog::info("SaveFileTestManager::SaveData - complete");
}

void SaveFileTestManager::LoadData(SKSE::SerializationInterface* intfc, uint32_t type, uint32_t version, uint32_t length)
{
    if (type != kRecordType) {
        return;
    }

    if (version != kDataVersion) {
        spdlog::warn("SaveFileTestManager::LoadData - version mismatch ({} vs {})", version, kDataVersion);
        return;
    }

    RE::FormID tempId = 0, nonTempId = 0;
    intfc->ReadRecordData(&tempId, sizeof(tempId));
    intfc->ReadRecordData(&nonTempId, sizeof(nonTempId));

    // Resolve old FormIDs to new FormIDs (handles load order changes)
    RE::FormID resolvedTemp = 0, resolvedNonTemp = 0;
    if (tempId != 0) {
        intfc->ResolveFormID(tempId, resolvedTemp);
    }
    if (nonTempId != 0) {
        intfc->ResolveFormID(nonTempId, resolvedNonTemp);
    }

    m_trackedTemporaryRefId = resolvedTemp;
    m_trackedNonTemporaryRefId = resolvedNonTemp;

    spdlog::info("SaveFileTestManager::LoadData - loaded tracked refs (temp:{:08X}->{:08X}, nonTemp:{:08X}->{:08X})",
        tempId, resolvedTemp, nonTempId, resolvedNonTemp);
}

void SaveFileTestManager::RevertData()
{
    m_trackedTemporaryRefId = 0;
    m_trackedNonTemporaryRefId = 0;
    spdlog::info("SaveFileTestManager::RevertData - cleared tracked refs");
}

} // namespace Test
