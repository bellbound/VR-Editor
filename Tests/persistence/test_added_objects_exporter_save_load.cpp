#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/AddedObjectsExporter.h"
#include "../../src/persistence/ChangedObjectRegistry.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// AddedObjectsExporter Save/Load Scenario Tests
// =============================================================================
// These tests simulate scenarios where saves are loaded on top of existing
// registry state, potentially causing data conflicts or loss for created objects.

TEST_CASE("AddedObjects Exporter - Fresh game with created objects", "[added][exporter][save-load]") {
    SECTION("New game starts with empty registry") {
        ChangedObjectRegistry registry;

        REQUIRE(registry.Count() == 0);
        REQUIRE(registry.GetAllEntries().empty());
    }

    SECTION("Loading save with created objects populates registry") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3D~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        REQUIRE(registry.Count() == 2);
        REQUIRE(registry.Contains("0xFF000001~DYNAMIC"));
        REQUIRE(registry.Contains("0xFF000002~DYNAMIC"));

        // Both should be marked as created
        auto state1 = registry.GetOriginalState("0xFF000001~DYNAMIC");
        auto state2 = registry.GetOriginalState("0xFF000002~DYNAMIC");
        REQUIRE(state1->wasCreated == true);
        REQUIRE(state2->wasCreated == true);
    }
}

TEST_CASE("AddedObjects Exporter - Load on top of existing created objects", "[added][exporter][save-load]") {
    SECTION("Loading different save replaces created objects") {
        ChangedObjectRegistry registry;

        // First save with created objects
        std::vector<ChangedObjectSaveGameData> initialData;
        initialData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(initialData));
        REQUIRE(registry.Count() == 1);

        // Clear (simulating game revert)
        registry.Clear();

        // Load different save
        std::vector<ChangedObjectSaveGameData> newData;
        newData.push_back(MakeCreatedSaveData(
            "0xFF000010~DYNAMIC", "0x22222~Skyrim.esm", "0x3D~Skyrim.esm"));
        newData.push_back(MakeCreatedSaveData(
            "0xFF000011~DYNAMIC", "0x33333~Skyrim.esm", "0x3D~Skyrim.esm"));
        registry.LoadEntries(std::move(newData));

        REQUIRE(registry.Count() == 2);
        REQUIRE_FALSE(registry.Contains("0xFF000001~DYNAMIC"));
        REQUIRE(registry.Contains("0xFF000010~DYNAMIC"));
        REQUIRE(registry.Contains("0xFF000011~DYNAMIC"));
    }

    SECTION("Unexported created objects are lost on save reload") {
        ChangedObjectRegistry registry;

        // Load initial save
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        // Modify position (mark pending)
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(999, 999, 999), "Test");

        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.size() == 1);

        // Reload save without exporting first
        registry.Clear();

        REQUIRE(registry.Count() == 0);
        REQUIRE(registry.GetPendingExportEntries().empty());
    }
}

TEST_CASE("AddedObjects Exporter - Same save reloaded", "[added][exporter][save-load]") {
    SECTION("Reloading same save restores identical state") {
        auto createSaveData = []() {
            std::vector<ChangedObjectSaveGameData> data;
            data.push_back(MakeCreatedSaveData(
                "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
            data.push_back(MakeCreatedSaveData(
                "0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3D~Skyrim.esm"));
            return data;
        };

        ChangedObjectRegistry registry;

        // First load
        registry.LoadEntries(createSaveData());
        size_t firstCount = registry.Count();

        // Clear and reload
        registry.Clear();
        registry.LoadEntries(createSaveData());
        size_t secondCount = registry.Count();

        REQUIRE(firstCount == secondCount);
    }

    SECTION("Export state resets on reload") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        // Make pending and export
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(100, 200, 300), "Test");
        REQUIRE(registry.GetPendingExportEntries().size() == 1);

        registry.ClearPendingExportFlagsForCreatedObjects();
        REQUIRE(registry.GetPendingExportEntries().empty());

        // Reload
        registry.Clear();
        std::vector<ChangedObjectSaveGameData> saveData2;
        saveData2.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData2));

        // Should start fresh (no pending)
        REQUIRE(registry.GetPendingExportEntries().empty());
    }
}

TEST_CASE("AddedObjects Exporter - Mix of created and existing through save/load", "[added][exporter][save-load]") {
    SECTION("Both created and modified objects persist through save/load") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeExistingSaveData("0x1002~Skyrim.esm", "0x3D~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        REQUIRE(registry.Count() == 3);

        size_t createdCount = 0;
        size_t existingCount = 0;
        for (const auto& [key, data] : registry.GetAllEntries()) {
            if (data.saveData.wasCreated) {
                createdCount++;
            } else {
                existingCount++;
            }
        }
        REQUIRE(createdCount == 1);
        REQUIRE(existingCount == 2);
    }

    SECTION("Separate pending flag clearing for created vs modified") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Update both
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(10, 20, 30), "Test");
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(100, 200, 300), "Test");

        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.size() == 2);

        // Clear only non-created (BOS export)
        registry.ClearPendingExportFlags();

        // Clear only created (AddedObjects export)
        registry.ClearPendingExportFlagsForCreatedObjects();

        REQUIRE(registry.GetPendingExportEntries().empty());
    }
}

TEST_CASE("AddedObjects Exporter - Deleted created objects through save/load", "[added][exporter][save-load]") {
    SECTION("Deleted created object state persists") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto created = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm");
        created.wasDeleted = true;
        saveData.push_back(std::move(created));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->wasCreated == true);
        REQUIRE(state->wasDeleted == true);
    }

    SECTION("pendingHardDelete flag persists through save/load") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto created = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm");
        created.pendingHardDelete = true;
        saveData.push_back(std::move(created));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->pendingHardDelete == true);
    }
}

TEST_CASE("AddedObjects Exporter - Base form key persistence", "[added][exporter][save-load]") {
    SECTION("Base form key is preserved through save/load") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->baseFormKey == "0x12345~Skyrim.esm");
    }

    SECTION("Different base forms for objects in same save") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000002~DYNAMIC", "0x67890~Dawnguard.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000003~DYNAMIC", "0xABCDE~MyMod.esp", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        auto state1 = registry.GetOriginalState("0xFF000001~DYNAMIC");
        auto state2 = registry.GetOriginalState("0xFF000002~DYNAMIC");
        auto state3 = registry.GetOriginalState("0xFF000003~DYNAMIC");

        REQUIRE(state1->baseFormKey == "0x12345~Skyrim.esm");
        REQUIRE(state2->baseFormKey == "0x67890~Dawnguard.esm");
        REQUIRE(state3->baseFormKey == "0xABCDE~MyMod.esp");
    }
}

TEST_CASE("AddedObjects Exporter - Original transform for created objects", "[added][exporter][save-load]") {
    SECTION("Original transform preserved after modifications") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto created = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm");
        created.originalTransform = MakeTransform(100, 200, 300);
        saveData.push_back(std::move(created));

        registry.LoadEntries(std::move(saveData));

        // Update current transform multiple times
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(500, 500, 500), "Test1");
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(600, 600, 600), "Test2");

        // Original should be preserved
        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->originalTransform.translate.x == Catch::Approx(100.0f));
        REQUIRE(state->originalTransform.translate.y == Catch::Approx(200.0f));
        REQUIRE(state->originalTransform.translate.z == Catch::Approx(300.0f));
    }
}

TEST_CASE("AddedObjects Exporter - Timestamp preservation", "[added][exporter][save-load]") {
    SECTION("Creation timestamp persists through save/load") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto created = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm");
        created.timestamp = 1609459200;  // Specific timestamp
        saveData.push_back(std::move(created));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->timestamp == 1609459200);
    }

    SECTION("Different objects can have different timestamps") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;

        auto created1 = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm");
        created1.timestamp = 1000;

        auto created2 = MakeCreatedSaveData(
            "0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3C~Skyrim.esm");
        created2.timestamp = 2000;

        saveData.push_back(std::move(created1));
        saveData.push_back(std::move(created2));

        registry.LoadEntries(std::move(saveData));

        auto state1 = registry.GetOriginalState("0xFF000001~DYNAMIC");
        auto state2 = registry.GetOriginalState("0xFF000002~DYNAMIC");

        REQUIRE(state1->timestamp == 1000);
        REQUIRE(state2->timestamp == 2000);
    }
}

TEST_CASE("AddedObjects Exporter - Cell info through save/load", "[added][exporter][save-load]") {
    SECTION("Cell FormKey and EditorID both persist") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto created = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", "WhiterunExterior01");
        saveData.push_back(std::move(created));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->cellFormKey == "0x3C~Skyrim.esm");
        REQUIRE(state->cellEditorId == "WhiterunExterior01");
    }

    SECTION("Objects from different cells maintain cell info") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", "WhiterunExterior01"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3D~Skyrim.esm", "RiftenExterior01"));

        registry.LoadEntries(std::move(saveData));

        auto state1 = registry.GetOriginalState("0xFF000001~DYNAMIC");
        auto state2 = registry.GetOriginalState("0xFF000002~DYNAMIC");

        REQUIRE(state1->cellFormKey == "0x3C~Skyrim.esm");
        REQUIRE(state1->cellEditorId == "WhiterunExterior01");
        REQUIRE(state2->cellFormKey == "0x3D~Skyrim.esm");
        REQUIRE(state2->cellEditorId == "RiftenExterior01");
    }
}

TEST_CASE("AddedObjects Exporter - Session-only vs persisted state", "[added][exporter][save-load]") {
    SECTION("Loaded objects are not session-only") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        const auto& entries = registry.GetAllEntries();
        for (const auto& [key, data] : entries) {
            REQUIRE(data.createdThisSession == false);
        }
    }

    SECTION("Undo with random action ID doesn't affect loaded objects") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));
        REQUIRE(registry.Count() == 1);

        // Try to undo with various action IDs
        for (int i = 0; i < 10; i++) {
            registry.OnActionUndone(Util::UUID::Generate());
        }

        // Loaded object should still exist
        REQUIRE(registry.Count() == 1);
        REQUIRE(registry.Contains("0xFF000001~DYNAMIC"));
    }
}

TEST_CASE("AddedObjects Exporter - Large number of created objects", "[added][exporter][save-load]") {
    SECTION("Many created objects can be loaded") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        for (int i = 0; i < 100; i++) {
            saveData.push_back(MakeCreatedSaveData(
                "0xFF" + std::to_string(100000 + i) + "~DYNAMIC",
                "0x" + std::to_string(10000 + i) + "~Skyrim.esm",
                "0x3C~Skyrim.esm"));
        }

        registry.LoadEntries(std::move(saveData));

        REQUIRE(registry.Count() == 100);
    }

    SECTION("Created objects in multiple cells can be loaded") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        std::vector<std::string> cellFormKeys = {
            "0x3C~Skyrim.esm", "0x3D~Skyrim.esm", "0x3E~Skyrim.esm",
            "0x3F~Skyrim.esm", "0x40~Skyrim.esm"
        };

        for (int i = 0; i < 50; i++) {
            saveData.push_back(MakeCreatedSaveData(
                "0xFF" + std::to_string(100000 + i) + "~DYNAMIC",
                "0x" + std::to_string(10000 + i) + "~Skyrim.esm",
                cellFormKeys[i % 5]));
        }

        registry.LoadEntries(std::move(saveData));

        REQUIRE(registry.Count() == 50);

        // Verify distribution across cells
        std::map<std::string, int> cellCounts;
        for (const auto& [key, data] : registry.GetAllEntries()) {
            cellCounts[data.saveData.cellFormKey]++;
        }
        REQUIRE(cellCounts.size() == 5);
    }
}
