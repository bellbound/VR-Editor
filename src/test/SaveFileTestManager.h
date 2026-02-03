#pragma once

#include "../IFrameUpdateListener.h"
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESCellAttachDetachEvent.h>
#include <SKSE/SKSE.h>
#include <vector>

namespace Test {

// Test manager for save file behavior with temporary vs non-temporary refs.
//
// Spawns 100 temporary refs (kTemporary flag) and 100 non-temporary refs.
// Stores one representative FormID from each group in the save file.
// On cell change, checks if those refs are still loaded.
class SaveFileTestManager : public IFrameUpdateListener,
                            public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
{
public:
    static SaveFileTestManager* GetSingleton();

    void Initialize();
    // Called by SaveGameDataManager during its callbacks
    void SaveData(SKSE::SerializationInterface* intfc);
    void LoadData(SKSE::SerializationInterface* intfc, uint32_t type, uint32_t version, uint32_t length);
    void RevertData();
    void StartSpawning();
    void Reset();

    // Delete all dynamic refs (FormID >= 0xFF000000) - disabled but kept
    void DeleteAllDynamicRefs();

    // IFrameUpdateListener
    void OnFrameUpdate(float deltaTime) override;

    // Cell change event
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellAttachDetachEvent* a_event,
        RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override;

    bool IsSpawning() const { return m_isSpawning; }
    size_t GetSpawnedCount() const { return m_spawnedRefs.size(); }

    // Record type for serialization (used by SaveGameDataManager)
    static constexpr uint32_t kRecordType = 'TSET';  // "TEST" reversed
    static constexpr uint32_t kDataVersion = 1;

private:
    SaveFileTestManager() = default;
    ~SaveFileTestManager() = default;
    SaveFileTestManager(const SaveFileTestManager&) = delete;
    SaveFileTestManager& operator=(const SaveFileTestManager&) = delete;

    RE::TESObjectREFR* SpawnAtPosition(const RE::NiPoint3& pos, bool makeTemporary);
    void CheckTrackedRefs();

    // Spawn settings
    static constexpr int TEMPORARY_COUNT = 100;
    static constexpr int NON_TEMPORARY_COUNT = 100;
    static constexpr int TOTAL_OBJECTS = TEMPORARY_COUNT + NON_TEMPORARY_COUNT;
    static constexpr float SPAWN_RATE = 50.0f;  // objects per second
    static constexpr float CIRCLE_RADIUS = 1200.0f;
    static constexpr RE::FormID BASE_FORM_ID = 0x077FFC;  // Static object base ID
    static constexpr const char* PLUGIN_NAME = "Skyrim.esm";

    bool m_initialized = false;
    bool m_isSpawning = false;
    int m_objectsToSpawn = 0;
    int m_temporarySpawned = 0;
    int m_nonTemporarySpawned = 0;
    float m_spawnAccumulator = 0.0f;
    RE::NiPoint3 m_circleCenter;
    std::vector<RE::TESObjectREFR*> m_spawnedRefs;

    // Tracked refs for persistence testing
    RE::FormID m_trackedTemporaryRefId = 0;
    RE::FormID m_trackedNonTemporaryRefId = 0;
};

} // namespace Test
