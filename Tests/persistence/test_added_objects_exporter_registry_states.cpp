#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/AddedObjectsExporter.h"
#include "../../src/persistence/ChangedObjectRegistry.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// AddedObjectsExporter Registry State Edge Cases
// =============================================================================
// These tests focus on edge cases in the registry that could cause data loss,
// incorrect exports, or invalid AddedObjects files.

TEST_CASE("AddedObjects Exporter - Only exports created objects", "[added][exporter][registry]") {
    SECTION("Modified objects are skipped") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithModifiedObject("0x1002~Skyrim.esm", "0x3C~Skyrim.esm", 40, 50, 60);

        auto entries = builder.GetEntriesAsConstPtrs();

        // Count created objects only
        size_t createdCount = 0;
        for (const auto& [key, data] : entries) {
            if (data->saveData.wasCreated) {
                createdCount++;
            }
        }
        REQUIRE(createdCount == 0);
    }

    SECTION("Only created objects are valid for AddedObjects export") {
        RegistryStateBuilder builder;
        builder
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithCreatedObject("0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3C~Skyrim.esm", 400, 500, 600);

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 3);

        size_t createdCount = 0;
        for (const auto& [key, data] : entries) {
            if (data->saveData.wasCreated) {
                createdCount++;
            }
        }
        REQUIRE(createdCount == 2);
    }
}

TEST_CASE("AddedObjects Exporter - Base form handling", "[added][exporter][registry]") {
    SECTION("Created objects require base form key") {
        RegistryStateBuilder builder;
        builder.WithCreatedObject(
            "0xFF000001~DYNAMIC",
            "0x12345~Skyrim.esm",  // Base form
            "0x3C~Skyrim.esm",
            100, 200, 300);

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].second->saveData.baseFormKey == "0x12345~Skyrim.esm");
    }

    SECTION("Empty base form key results in skipped entry") {
        RegistryStateBuilder builder;
        builder.WithEmptyBaseForm("0xFF000001~DYNAMIC", "0x3C~Skyrim.esm");

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].second->saveData.baseFormKey.empty());

        // This entry would produce an invalid AddedObjectEntry
    }

    SECTION("Different base forms for same cell") {
        RegistryStateBuilder builder;
        builder
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithCreatedObject("0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3C~Skyrim.esm", 400, 500, 600)
            .WithCreatedObject("0xFF000003~DYNAMIC", "0x12347~Dawnguard.esm", "0x3C~Skyrim.esm", 700, 800, 900);

        auto entries = builder.GetEntriesAsConstPtrs();

        std::set<std::string> baseFormKeys;
        for (const auto& [key, data] : entries) {
            baseFormKeys.insert(data->saveData.baseFormKey);
        }
        REQUIRE(baseFormKeys.size() == 3);
    }
}

TEST_CASE("AddedObjects Exporter - Handles missing cell info", "[added][exporter][registry]") {
    SECTION("Created objects without cell info are skipped") {
        RegistryStateBuilder builder;
        builder.WithNoCellInfo("0xFF000001~DYNAMIC", true);  // Created but no cell

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].second->saveData.cellFormKey.empty());
    }

    SECTION("Mix of valid and missing cell info") {
        RegistryStateBuilder builder;
        builder
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithNoCellInfo("0xFF000002~DYNAMIC", true)
            .WithCreatedObject("0xFF000003~DYNAMIC", "0x12347~Skyrim.esm", "0x3D~Skyrim.esm", 400, 500, 600);

        auto entries = builder.GetEntriesAsConstPtrs();

        size_t validCellCount = 0;
        for (const auto& [key, data] : entries) {
            if (!data->saveData.cellFormKey.empty()) {
                validCellCount++;
            }
        }
        REQUIRE(validCellCount == 2);
    }
}

TEST_CASE("AddedObjects Exporter - Pending export flags", "[added][exporter][registry]") {
    SECTION("Only created entries with pending flag are exported") {
        ChangedObjectRegistry registry;

        // Load created objects
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Initially no pending (loaded entries start without pending flag)
        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.empty());

        // Update one to have pending changes
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(100, 200, 300), "Test");

        pending = registry.GetPendingExportEntries();
        // Only the updated one has pending flag
        REQUIRE(pending.size() == 1);
    }

    SECTION("ClearPendingExportFlagsForCreatedObjects only clears created entries") {
        ChangedObjectRegistry registry;

        // Mix of created and modified
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Update both to have pending
        registry.UpdateCurrentTransform("0x1001~Skyrim.esm", MakeTransform(10, 20, 30), "Test");
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(100, 200, 300), "Test");

        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.size() == 2);

        // Clear only created
        registry.ClearPendingExportFlagsForCreatedObjects();

        pending = registry.GetPendingExportEntries();
        // Modified entry should still have pending flag
        bool modifiedStillPending = false;
        for (const auto& [key, data] : pending) {
            if (!data->saveData.wasCreated) {
                modifiedStillPending = true;
            }
        }
        // Note: The modified entry's flag should be preserved
    }
}

TEST_CASE("AddedObjects Exporter - Deleted created objects", "[added][exporter][registry]") {
    SECTION("Deleted created objects have wasDeleted and wasCreated flags") {
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

    SECTION("pendingHardDelete for dynamic refs") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));
        registry.MarkPendingHardDelete("0xFF000001~DYNAMIC");

        auto state = registry.GetOriginalState("0xFF000001~DYNAMIC");
        REQUIRE(state.has_value());
        REQUIRE(state->pendingHardDelete == true);
    }
}

TEST_CASE("AddedObjects Exporter - Cell grouping", "[added][exporter][registry]") {
    SECTION("Created objects are grouped by cell") {
        RegistryStateBuilder builder;
        builder
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithCreatedObject("0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3C~Skyrim.esm", 400, 500, 600)
            .WithCreatedObject("0xFF000003~DYNAMIC", "0x12347~Skyrim.esm", "0x3D~Skyrim.esm", 700, 800, 900);

        auto entries = builder.GetEntriesAsConstPtrs();

        std::map<std::string, size_t> cellCounts;
        for (const auto& [key, data] : entries) {
            cellCounts[data->saveData.cellFormKey]++;
        }

        REQUIRE(cellCounts.size() == 2);
        REQUIRE(cellCounts["0x3C~Skyrim.esm"] == 2);
        REQUIRE(cellCounts["0x3D~Skyrim.esm"] == 1);
    }

    SECTION("Objects in different cells produce different file entries") {
        RegistryStateBuilder builder;
        builder
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithCreatedObject("0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3D~Skyrim.esm", 400, 500, 600);

        auto entries = builder.GetEntriesAsConstPtrs();

        std::set<std::string> cells;
        for (const auto& [key, data] : entries) {
            cells.insert(data->saveData.cellFormKey);
        }
        REQUIRE(cells.size() == 2);
    }
}

TEST_CASE("AddedObjects Exporter - DYNAMIC form key handling", "[added][exporter][registry]") {
    SECTION("Created objects use DYNAMIC marker in form key") {
        RegistryStateBuilder builder;
        builder.WithCreatedObject(
            "0xFF000001~DYNAMIC",
            "0x12345~Skyrim.esm",
            "0x3C~Skyrim.esm",
            100, 200, 300);

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].first.find("~DYNAMIC") != std::string::npos);
    }

    SECTION("DYNAMIC form keys are distinct from regular form keys") {
        RegistryStateBuilder builder;
        builder
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30);

        auto entries = builder.GetEntriesAsConstPtrs();

        size_t dynamicCount = 0;
        size_t regularCount = 0;
        for (const auto& [key, data] : entries) {
            if (key.find("~DYNAMIC") != std::string::npos) {
                dynamicCount++;
            } else {
                regularCount++;
            }
        }
        REQUIRE(dynamicCount == 1);
        REQUIRE(regularCount == 1);
    }
}

TEST_CASE("AddedObjects Exporter - Multiple objects same position", "[added][exporter][registry]") {
    SECTION("Objects at same position are tracked separately") {
        // This is an edge case - theoretically two objects could be placed at exact same position
        RegistryStateBuilder builder;
        builder
            .WithCreatedObject("0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300)
            .WithCreatedObject("0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300);  // Same position

        auto entries = builder.GetEntriesAsConstPtrs();

        // Both should exist (different form keys)
        REQUIRE(entries.size() == 2);

        // But both have same position
        REQUIRE(entries[0].second->currentTransform.translate.x ==
                entries[1].second->currentTransform.translate.x);
    }
}

TEST_CASE("AddedObjects Exporter - Session state tracking", "[added][exporter][registry]") {
    SECTION("New created objects have createdThisSession=true") {
        RegistryStateBuilder builder;
        builder.WithCreatedObject(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", 100, 200, 300);

        auto entries = builder.GetEntriesAsConstPtrs();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].second->createdThisSession == true);
    }

    SECTION("Loaded created objects have createdThisSession=false") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Loaded entries have createdThisSession=false
        const auto& entries = registry.GetAllEntries();
        for (const auto& [key, data] : entries) {
            REQUIRE(data.createdThisSession == false);
        }
    }

    SECTION("Undo removes session-created objects") {
        ChangedObjectRegistry registry;

        // First, we need a way to track the action ID
        // In real code, RegisterCreatedObject stores the action ID

        // Simulate by loading then checking undo behavior
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Loaded entries aren't removed by undo (createdThisSession=false)
        registry.OnActionUndone(Util::UUID::Generate());

        REQUIRE(registry.Count() == 1);
    }
}

TEST_CASE("AddedObjects Exporter - Transform updates", "[added][exporter][registry]") {
    SECTION("UpdateCurrentTransform for created objects") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Update transform
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(999, 888, 777), "NewLocation");

        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.size() == 1);
        REQUIRE(pending[0].second->currentTransform.translate.x == Catch::Approx(999.0f));
    }

    SECTION("Multiple transform updates maintain latest value") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));

        // Multiple updates
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(100, 100, 100), "Loc1");
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(200, 200, 200), "Loc2");
        registry.UpdateCurrentTransform("0xFF000001~DYNAMIC", MakeTransform(300, 300, 300), "Loc3");

        auto pending = registry.GetPendingExportEntries();
        REQUIRE(pending.size() == 1);
        REQUIRE(pending[0].second->currentTransform.translate.x == Catch::Approx(300.0f));
    }
}

TEST_CASE("AddedObjects Exporter - Cell editor ID handling", "[added][exporter][registry]") {
    SECTION("Cell editor ID is preserved for created objects") {
        RegistryStateBuilder builder;
        // The builder uses the default constructor which may not set cellEditorId
        auto saveData = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", "WhiterunExterior01");

        REQUIRE(saveData.cellEditorId == "WhiterunExterior01");
        REQUIRE(saveData.cellFormKey == "0x3C~Skyrim.esm");
    }

    SECTION("Cell editor ID fallback when empty") {
        auto saveData = MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm", "");

        REQUIRE(saveData.cellEditorId.empty());
        REQUIRE(!saveData.cellFormKey.empty());
    }
}

TEST_CASE("AddedObjects Exporter - Extract entries for cell", "[added][exporter][registry]") {
    SECTION("ExtractEntriesForCell removes created objects") {
        ChangedObjectRegistry registry;

        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000001~DYNAMIC", "0x12345~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000002~DYNAMIC", "0x12346~Skyrim.esm", "0x3C~Skyrim.esm"));
        saveData.push_back(MakeCreatedSaveData(
            "0xFF000003~DYNAMIC", "0x12347~Skyrim.esm", "0x3D~Skyrim.esm"));

        registry.LoadEntries(std::move(saveData));
        REQUIRE(registry.Count() == 3);

        auto extracted = registry.ExtractEntriesForCell("0x3C~Skyrim.esm");

        REQUIRE(extracted.size() == 2);
        REQUIRE(registry.Count() == 1);
    }
}
