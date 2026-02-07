#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/BaseObjectSwapperExporter.h"
#include "../../src/persistence/ChangedObjectRegistry.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// BaseObjectSwapperExporter Save/Load Scenario Tests
// =============================================================================
// These tests simulate scenarios where saves are loaded on top of existing
// registry state, potentially causing data conflicts or loss.

TEST_CASE("BOS Exporter - Fresh game start", "[bos][exporter][save-load]") {
    SECTION("Empty registry on fresh start") {
        ChangedObjectRegistry registry;

        REQUIRE(registry.Count() == 0);
        REQUIRE(registry.GetAllEntries().empty());
        REQUIRE(registry.GetPendingExportEntries().empty());
    }

    SECTION("Loading save populates registry") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeExistingSaveData("0x1002~Skyrim.esm", "0x3D~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        REQUIRE(registry.Count() == 3);
        REQUIRE(registry.Contains("0x1001~Skyrim.esm"));
        REQUIRE(registry.Contains("0x1002~Skyrim.esm"));
        REQUIRE(registry.Contains("0xFF000001~DYNAMIC"));
    }
}

TEST_CASE("BOS Exporter - Load on top of existing state", "[bos][exporter][save-load]") {
    SECTION("Loading save clears and replaces existing entries") {
        ChangedObjectRegistry registry;

        // Initial state
        std::vector<ChangedObjectSaveGameData> initialData;
        initialData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(initialData));
        REQUIRE(registry.Count() == 1);

        // Clear before loading new save (as would happen in game)
        registry.Clear();

        // Load different save
        std::vector<ChangedObjectSaveGameData> newData;
        newData.push_back(MakeExistingSaveData("0x2001~Skyrim.esm", "0x3D~Skyrim.esm"));
        newData.push_back(MakeExistingSaveData("0x2002~Skyrim.esm", "0x3D~Skyrim.esm"));
        registry.LoadEntries(std::move(newData));

        REQUIRE(registry.Count() == 2);
        REQUIRE_FALSE(registry.Contains("0x1001~Skyrim.esm"));
        REQUIRE(registry.Contains("0x2001~Skyrim.esm"));
    }

    SECTION("Pending changes are lost on save reload if not exported") {
        ChangedObjectRegistry registry;

        // Load save
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        // Make changes (mark pending)
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(999, 999, 999), "SomeLocation");
        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.size() == 1);

        // Clear (simulating loading a different save without saving first)
        registry.Clear();

        REQUIRE(registry.Count() == 0);
        REQUIRE(registry.GetPendingExportEntries().empty());
    }
}

TEST_CASE("BOS Exporter - Same save loaded multiple times", "[bos][exporter][save-load]") {
    SECTION("Reloading same save should restore same state") {
        auto createSaveData = []() {
            std::vector<ChangedObjectSaveGameData> data;
            data.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
            data.push_back(MakeExistingSaveData("0x1002~Skyrim.esm", "0x3D~Skyrim.esm"));
            return data;
        };

        ChangedObjectRegistry registry;

        // First load
        registry.LoadEntries(createSaveData());
        size_t firstCount = registry.Count();
        bool firstContains1001 = registry.Contains("0x1001~Skyrim.esm");

        // Clear and reload
        registry.Clear();
        registry.LoadEntries(createSaveData());
        size_t secondCount = registry.Count();
        bool secondContains1001 = registry.Contains("0x1001~Skyrim.esm");

        REQUIRE(firstCount == secondCount);
        REQUIRE(firstContains1001 == secondContains1001);
    }

    SECTION("Export state resets on reload") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        // Make pending changes
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(100, 200, 300), "Test");
        REQUIRE(registry.GetPendingExportEntries().size() == 1);

        // Export and clear flags
        registry.ClearPendingExportFlags();
        REQUIRE(registry.GetPendingExportEntries().empty());

        // Reload same entry
        registry.Clear();
        std::vector<ChangedObjectSaveGameData> saveData2;
        saveData2.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData2));

        // Should start with no pending (loaded entries don't have pending flag)
        REQUIRE(registry.GetPendingExportEntries().empty());
    }
}

TEST_CASE("BOS Exporter - Incremental save scenarios", "[bos][exporter][save-load]") {
    SECTION("Multiple modifications to same object") {
        ChangedObjectRegistry registry;

        // Load initial state
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        // First modification
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(10, 10, 10), "Location1");
        auto pending1 = registry.GetPendingExportEntries();
        REQUIRE(pending1.size() == 1);

        // Second modification (updates existing)
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(20, 20, 20), "Location2");
        auto pending2 = registry.GetPendingExportEntries();
        REQUIRE(pending2.size() == 1);  // Still just one entry

        // The transform should be the latest
        REQUIRE(pending2[0].second->currentTransform.translate.x == Catch::Approx(20.0f));
    }

    SECTION("Export clears pending, new changes create new pending") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        // Modify and export
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(10, 10, 10), "Test");
        REQUIRE(registry.GetPendingExportEntries().size() == 1);
        registry.ClearPendingExportFlags();
        REQUIRE(registry.GetPendingExportEntries().empty());

        // New modification
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(20, 20, 20), "Test2");
        REQUIRE(registry.GetPendingExportEntries().size() == 1);
    }

    SECTION("Added entries during session") {
        ChangedObjectRegistry registry;

        // Load initial save (empty registry scenario)
        // Then user makes modifications during play

        // Simulate modification action adding to registry
        // (In real code, ActionHistoryRepository::Add calls RegisterIfNew)
        std::vector<ChangedObjectSaveGameData> newEntries;
        newEntries.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(newEntries));

        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(50, 50, 50), "Whiterun");

        REQUIRE(registry.Count() == 1);
        REQUIRE(registry.GetPendingExportEntries().size() == 1);
    }
}

TEST_CASE("BOS Exporter - Save with deleted objects", "[bos][exporter][save-load]") {
    SECTION("Deleted flag persists through save/load") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto deleted = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm");
        deleted.wasDeleted = true;
        saveData.push_back(std::move(deleted));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0x1001~Skyrim.esm");
        REQUIRE(state.has_value());
        REQUIRE(state->wasDeleted == true);
    }

    SECTION("Undeleting requires removing from registry or updating flag") {
        // This is a conceptual test - undeleting would require special handling
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto deleted = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm");
        deleted.wasDeleted = true;
        saveData.push_back(std::move(deleted));

        registry.LoadEntries(std::move(saveData));

        // Currently no API to undelete - would need to clear and re-add
        // or add an UpdateDeletedFlag method
    }
}

TEST_CASE("BOS Exporter - Created objects through save/load", "[bos][exporter][save-load]") {
    SECTION("Created objects persist with wasCreated flag") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->wasCreated == true);
        REQUIRE(state->baseFormKey == "0x12345~Skyrim.esm");
    }

    SECTION("Created objects need base form key for respawning") {
        ChangedObjectRegistry registry;

        // Created object without base form key (error case)
        std::vector<ChangedObjectSaveGameData> saveData;
        auto created = MakeCreatedSaveData("0xFF000001~DYNAMIC", "", "0x3C~Skyrim.esm");
        created.baseFormKey = "";  // Simulate missing data
        saveData.push_back(std::move(created));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->baseFormKey.empty());  // This would be a problem for respawning
    }

    SECTION("pendingHardDelete flag for created object deletion") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto created = MakeCreatedSaveData("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm");
        created.pendingHardDelete = true;
        saveData.push_back(std::move(created));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->pendingHardDelete == true);
    }
}

TEST_CASE("BOS Exporter - Original transform preservation", "[bos][exporter][save-load]") {
    SECTION("Original transform is preserved even after multiple changes") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto data = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm");
        data.originalTransform = MakeTransform(0, 0, 0);  // Original at origin
        saveData.push_back(std::move(data));

        registry.LoadEntries(std::move(saveData));

        // Update current transform multiple times
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(100, 100, 100), "Test");
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(200, 200, 200), "Test");
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(300, 300, 300), "Test");

        // Original should still be at origin
        auto state = registry.GetOriginalState("0x1001~Skyrim.esm");
        REQUIRE(state.has_value());
        REQUIRE(state->originalTransform.translate.x == Catch::Approx(0.0f));
        REQUIRE(state->originalTransform.translate.y == Catch::Approx(0.0f));
        REQUIRE(state->originalTransform.translate.z == Catch::Approx(0.0f));
    }

    SECTION("Original transform used for undo restoration") {
        // This is a conceptual test - undo would use the original transform
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto data = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm");
        data.originalTransform = MakeTransform(50, 75, 100);
        saveData.push_back(std::move(data));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0x1001~Skyrim.esm");
        REQUIRE(state.has_value());

        // This original transform would be used by undo to restore the object
        REQUIRE(state->originalTransform.translate.x == Catch::Approx(50.0f));
        REQUIRE(state->originalTransform.translate.y == Catch::Approx(75.0f));
        REQUIRE(state->originalTransform.translate.z == Catch::Approx(100.0f));
    }
}

TEST_CASE("BOS Exporter - Timestamp handling", "[bos][exporter][save-load]") {
    SECTION("Timestamp is preserved through save/load") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto data = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm");
        data.timestamp = 1609459200;  // 2021-01-01 00:00:00 UTC
        saveData.push_back(std::move(data));

        registry.LoadEntries(std::move(saveData));

        auto state = registry.GetOriginalState("0x1001~Skyrim.esm");
        REQUIRE(state.has_value());
        REQUIRE(state->timestamp == 1609459200);
    }

    SECTION("Entries with different timestamps are distinct") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;

        auto data1 = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm");
        data1.timestamp = 1000;

        auto data2 = MakeExistingSaveData("0x1002~Skyrim.esm", "0x3C~Skyrim.esm");
        data2.timestamp = 2000;

        saveData.push_back(std::move(data1));
        saveData.push_back(std::move(data2));

        registry.LoadEntries(std::move(saveData));

        auto state1 = registry.GetOriginalState("0x1001~Skyrim.esm");
        auto state2 = registry.GetOriginalState("0x1002~Skyrim.esm");

        REQUIRE(state1->timestamp == 1000);
        REQUIRE(state2->timestamp == 2000);
    }
}

TEST_CASE("BOS Exporter - Cell extraction for cleanup", "[bos][exporter][save-load]") {
    SECTION("ExtractEntriesForCell removes entries from registry") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeExistingSaveData("0x1002~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeExistingSaveData("0x1003~Skyrim.esm", "0x3D~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));
        REQUIRE(registry.Count() == 3);

        auto extracted = registry.ExtractEntriesForCell("0x3C~Skyrim.esm");

        REQUIRE(extracted.size() == 2);
        REQUIRE(registry.Count() == 1);
        REQUIRE_FALSE(registry.Contains("0x1001~Skyrim.esm"));
        REQUIRE_FALSE(registry.Contains("0x1002~Skyrim.esm"));
        REQUIRE(registry.Contains("0x1003~Skyrim.esm"));
    }

    SECTION("ExtractEntriesForCell with empty cell key returns nothing") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        auto extracted = registry.ExtractEntriesForCell("");

        REQUIRE(extracted.empty());
        REQUIRE(registry.Count() == 1);
    }

    SECTION("ExtractEntriesForCell with non-existent cell returns nothing") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        auto extracted = registry.ExtractEntriesForCell("0xNONEXISTENT~Test.esp");

        REQUIRE(extracted.empty());
        REQUIRE(registry.Count() == 1);
    }
}
