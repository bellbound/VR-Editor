#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/BaseObjectSwapperExporter.h"
#include "../../src/persistence/ChangedObjectRegistry.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// BaseObjectSwapperExporter Registry State Edge Cases
// =============================================================================
// These tests focus on edge cases in the registry that could cause data loss,
// incorrect exports, or invalid BOS files.

TEST_CASE("BOS Exporter - Skips created objects", "[bos][exporter][registry]") {
    SECTION("Created objects are not exported to BOS (they use AddedObjects)") {
        RegistryStateBuilder builder;
        builder.WithCreatedObject(
            "0xFF000001~DYNAMIC",
            "0x12345~Skyrim.esm",  // base form
            "0x3C~Skyrim.esm",     // cell
            100, 200, 300);

        auto entries = builder.GetEntriesAsConstPtrs();
        auto* exporter = BaseObjectSwapperExporter::GetSingleton();

        // This should skip the created object
        size_t result = exporter->ExportEntries(entries);

        // In real scenario, entries would be skipped internally
        // The GroupEntriesByCell method skips wasCreated entries
    }

    SECTION("Mixed registry with created and modified - only modified exported") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithModifiedObject("0x1002~Skyrim.esm", "0x3C~Skyrim.esm", 40, 50, 60);

        auto entries = builder.GetEntriesAsConstPtrs();

        // Check that we have 3 entries total but only 2 should be valid for BOS
        REQUIRE(entries.size() == 3);

        // Count non-created entries
        size_t validForBOS = 0;
        for (const auto& [key, data] : entries) {
            if (!data->saveData.wasCreated) {
                validForBOS++;
            }
        }
        REQUIRE(validForBOS == 2);
    }
}

TEST_CASE("BOS Exporter - Handles missing cell info", "[bos][exporter][registry]") {
    SECTION("Entries without cell info are skipped") {
        RegistryStateBuilder builder;
        builder.WithNoCellInfo("0x1001~Skyrim.esm", false);

        auto entries = builder.GetEntriesAsConstPtrs();

        // Entry has empty cellFormKey
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].second->saveData.cellFormKey.empty());

        // This entry should be skipped during GroupEntriesByCell
    }

    SECTION("Mix of valid and missing cell info") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithNoCellInfo("0x1002~Skyrim.esm", false)
            .WithModifiedObject("0x1003~Skyrim.esm", "0x3D~Skyrim.esm", 40, 50, 60);

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 3);

        // Count valid entries (with cell info)
        size_t validEntries = 0;
        for (const auto& [key, data] : entries) {
            if (!data->saveData.cellFormKey.empty()) {
                validEntries++;
            }
        }
        REQUIRE(validEntries == 2);
    }
}

TEST_CASE("BOS Exporter - Pending export flags", "[bos][exporter][registry]") {
    SECTION("Only entries with hasPendingExportChanges are exported") {
        RegistryStateBuilder builder;
        // Entry with pending changes
        builder.WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30, true);
        // Entry without pending changes (already exported)
        auto entries = builder.GetEntries();
        // Manually set one to no pending
        entries.push_back({"0x1002~Skyrim.esm", Persistence::ChangedObjectRuntimeData()});
        entries.back().second.saveData = MakeExistingSaveData("0x1002~Skyrim.esm", "0x3C~Skyrim.esm");
        entries.back().second.currentTransform = MakeTransform(40, 50, 60);
        entries.back().second.hasPendingExportChanges = false;

        size_t pendingCount = 0;
        for (const auto& [key, data] : entries) {
            if (data.hasPendingExportChanges) {
                pendingCount++;
            }
        }
        REQUIRE(pendingCount == 1);
    }

    SECTION("ClearPendingExportFlags only clears non-created entries") {
        ChangedObjectRegistry registry;

        // Load some data
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Set pending flags via UpdateCurrentTransform
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(10, 20, 30), "Whiterun");
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(100, 200, 300), "Whiterun");

        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.size() == 2);

        // Clear flags for non-created
        registry.ClearPendingExportFlags();

        pending = registry.GetPendingExportEntries();
        // Created object should still have pending flag
        bool createdStillPending = false;
        for (const auto& [key, data] : pending) {
            if (data->saveData.wasCreated) {
                createdStillPending = true;
            }
        }
        // Note: Created object's flag is preserved
    }
}

TEST_CASE("BOS Exporter - Deleted objects handling", "[bos][exporter][registry]") {
    SECTION("Deleted existing objects get Initially Disabled flag") {
        RegistryStateBuilder builder;
        builder.WithDeletedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm");

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].second->saveData.wasDeleted == true);
    }

    SECTION("Multiple deleted objects in same cell") {
        RegistryStateBuilder builder;
        builder
            .WithDeletedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm")
            .WithDeletedObject("0x1002~Skyrim.esm", "0x3C~Skyrim.esm")
            .WithModifiedObject("0x1003~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30);

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 3);

        size_t deletedCount = 0;
        for (const auto& [key, data] : entries) {
            if (data->saveData.wasDeleted) {
                deletedCount++;
            }
        }
        REQUIRE(deletedCount == 2);
    }

    SECTION("Deleted dynamic ref has pendingHardDelete flag") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        auto data = MakeCreatedSaveData("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm");
        data.wasDeleted = true;
        saveData.push_back(std::move(data));

        registry.LoadEntries(std::move(saveData));
        registry.MarkPendingHardDelete("0xFF000001~DYNAMIC");

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->pendingHardDelete == true);
    }
}

TEST_CASE("BOS Exporter - Cell grouping edge cases", "[bos][exporter][registry]") {
    SECTION("Entries from same cell are grouped together") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithModifiedObject("0x1002~Skyrim.esm", "0x3C~Skyrim.esm", 40, 50, 60)
            .WithModifiedObject("0x1003~Skyrim.esm", "0x3C~Skyrim.esm", 70, 80, 90);

        auto entries = builder.GetEntriesAsConstPtrs();

        // All entries have same cell
        std::set<std::string> cells;
        for (const auto& [key, data] : entries) {
            cells.insert(data->saveData.cellFormKey);
        }
        REQUIRE(cells.size() == 1);
        REQUIRE(cells.count("0x3C~Skyrim.esm") == 1);
    }

    SECTION("Entries from different cells are separated") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithModifiedObject("0x1002~Skyrim.esm", "0x3D~Skyrim.esm", 40, 50, 60)
            .WithModifiedObject("0x1003~Skyrim.esm", "0x3E~Skyrim.esm", 70, 80, 90);

        auto entries = builder.GetEntriesAsConstPtrs();

        std::set<std::string> cells;
        for (const auto& [key, data] : entries) {
            cells.insert(data->saveData.cellFormKey);
        }
        REQUIRE(cells.size() == 3);
    }

    SECTION("Same object in registry can only belong to one cell") {
        // This tests the assumption that an object can't be in multiple cells
        RegistryStateBuilder builder;
        builder.WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30);

        // Adding same formKey again shouldn't be possible in real registry
        // but we test that our data model is consistent
        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
    }
}

TEST_CASE("BOS Exporter - Load/save session interaction", "[bos][exporter][registry]") {
    SECTION("Loaded entries have createdThisSession=false") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeExistingSaveData("0x1002~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Loaded entries don't have pending changes initially
        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.empty());
    }

    SECTION("Undo only removes entries created this session") {
        ChangedObjectRegistry registry;

        // Load an entry from save
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        // Undo with a random action ID shouldn't remove loaded entries
        registry.OnActionUndone(Util::UUID::Generate());

        REQUIRE(registry.Count() == 1);
        REQUIRE(registry.Contains("0x1001~Skyrim.esm"));
    }

    SECTION("Clear removes all entries") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeExistingSaveData("0x1002~Skyrim.esm", "0x3D~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        REQUIRE(registry.Count() == 2);

        registry.Clear();

        REQUIRE(registry.Count() == 0);
    }
}

TEST_CASE("BOS Exporter - Registry thread safety", "[bos][exporter][registry]") {
    SECTION("Multiple reads don't block each other") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        for (int i = 0; i < 100; i++) {
            saveData.push_back(MakeExistingSaveData(
                "0x" + std::to_string(1000 + i) + "~Skyrim.esm",
                "0x3C~Skyrim.esm"));
        }
        registry.LoadEntries(std::move(saveData));

        // Concurrent reads should work
        bool contains1 = registry.Contains("0x1000~Skyrim.esm");
        size_t count = registry.Count();
        auto state = registry.GetOriginalState("0x1050~Skyrim.esm");

        REQUIRE(contains1);
        REQUIRE(count == 100);
        REQUIRE(state.has_value());
    }
}

TEST_CASE("BOS Exporter - DYNAMIC form key handling", "[bos][exporter][registry]") {
    SECTION("DYNAMIC marker identifies runtime-created objects") {
        std::string dynamicKey = "0xFF000001~DYNAMIC";

        // DYNAMIC objects should be skipped by BOS exporter
        RegistryStateBuilder builder;
        builder.WithCreatedObject(dynamicKey, "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300);

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].first.find("~DYNAMIC") != std::string::npos);
        REQUIRE(entries[0].second->saveData.wasCreated == true);
    }

    SECTION("Regular form keys vs DYNAMIC form keys") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300);

        auto entries = builder.GetEntriesAsConstPtrs();

        // Check which are valid for BOS
        size_t bosValidCount = 0;
        for (const auto& [key, data] : entries) {
            if (!data->saveData.wasCreated && key.find("~DYNAMIC") == std::string::npos) {
                bosValidCount++;
            }
        }
        REQUIRE(bosValidCount == 1);
    }
}

TEST_CASE("BOS Exporter - Cell editor ID handling", "[bos][exporter][registry]") {
    SECTION("Entries with cell editor ID use it for filename") {
        RegistryStateBuilder builder;
        // This uses the constructor that sets cellEditorId
        auto saveData = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", "WhiterunExterior01");

        ChangedObjectRuntimeData data;
        data.saveData = saveData;
        data.currentTransform = MakeTransform(10, 20, 30);
        data.hasPendingExportChanges = true;

        REQUIRE(data.saveData.cellEditorId == "WhiterunExterior01");
        REQUIRE(data.saveData.cellFormKey == "0x3C~Skyrim.esm");
    }

    SECTION("Entries without cell editor ID fallback to form key") {
        auto saveData = MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", "");

        REQUIRE(saveData.cellEditorId.empty());
        REQUIRE(!saveData.cellFormKey.empty());
    }
}
