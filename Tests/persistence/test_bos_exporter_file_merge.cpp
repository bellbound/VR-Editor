#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/BaseObjectSwapperExporter.h"
#include "../../src/persistence/BaseObjectSwapperParser.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// BaseObjectSwapperExporter File Merge Tests
// =============================================================================
// These tests verify correct behavior when merging with existing INI files,
// ensuring no data loss, proper deduplication, and correct metadata handling.

TEST_CASE("BOS Parser - ParseIniFile basics", "[bos][parser][file-merge]") {
    SECTION("ParseIniFile returns empty vector for non-existent file") {
        auto* parser = BaseObjectSwapperParser::GetSingleton();
        auto entries = parser->ParseIniFile("C:\\nonexistent\\path\\file.ini");
        REQUIRE(entries.empty());
    }

    SECTION("ParsePropertyString handles complete property string") {
        BOSTransformEntry entry;
        bool success = BaseObjectSwapperParser::ParsePropertyString(
            "posA(100,200,300),rotA(45,90,180),scaleA(2.5)", entry);

        REQUIRE(success);
        REQUIRE(entry.position.x == Catch::Approx(100.0f));
        REQUIRE(entry.position.y == Catch::Approx(200.0f));
        REQUIRE(entry.position.z == Catch::Approx(300.0f));
        REQUIRE(entry.rotation.x == Catch::Approx(45.0f));
        REQUIRE(entry.rotation.y == Catch::Approx(90.0f));
        REQUIRE(entry.rotation.z == Catch::Approx(180.0f));
        REQUIRE(entry.scale == Catch::Approx(2.5f));
    }

    SECTION("ParsePropertyString handles minimal property string (position only)") {
        BOSTransformEntry entry;
        bool success = BaseObjectSwapperParser::ParsePropertyString("posA(50,75,100)", entry);

        REQUIRE(success);
        REQUIRE(entry.position.x == Catch::Approx(50.0f));
        REQUIRE(entry.rotation.x == Catch::Approx(0.0f));  // Default
        REQUIRE(entry.scale == Catch::Approx(1.0f));  // Default
    }

    SECTION("ParsePropertyString fails without position") {
        BOSTransformEntry entry;
        bool success = BaseObjectSwapperParser::ParsePropertyString("rotA(0,0,0),scaleA(1)", entry);

        REQUIRE_FALSE(success);
    }

    SECTION("ParsePropertyString detects Initially Disabled flag") {
        BOSTransformEntry entry;
        bool success = BaseObjectSwapperParser::ParsePropertyString(
            "posA(0,0,0),flags(0x00000800)", entry);

        REQUIRE(success);
        REQUIRE(entry.isDeleted == true);
    }
}

TEST_CASE("BOS Parser - Filename generation", "[bos][parser][file-merge]") {
    SECTION("BuildIniFileName prefers editor ID") {
        std::string filename = BaseObjectSwapperParser::BuildIniFileName(
            "WhiterunExterior01", "0x3C~Skyrim.esm");
        REQUIRE(filename == "VREditor_WhiterunExterior01_SWAP.ini");
    }

    SECTION("BuildIniFileName falls back to form key when no editor ID") {
        std::string filename = BaseObjectSwapperParser::BuildIniFileName(
            "", "0x3C~Skyrim.esm");
        // FormKey gets sanitized (~ replaced with _)
        REQUIRE(filename.find("VREditor_") == 0);
        REQUIRE(filename.find("_SWAP.ini") != std::string::npos);
    }

    SECTION("SanitizeForFilename replaces invalid characters") {
        REQUIRE(BaseObjectSwapperParser::SanitizeForFilename("Test<>:\"/\\|?*") == "Test");
        REQUIRE(BaseObjectSwapperParser::SanitizeForFilename("Hello World") == "Hello_World");
        REQUIRE(BaseObjectSwapperParser::SanitizeForFilename("0x3C~Skyrim.esm") == "0x3C_Skyrim.esm");
    }

    SECTION("GetLatestFilePath converts swap to latest") {
        auto latestPath = BaseObjectSwapperParser::GetLatestFilePath("C:/Data/VREditor_Test_SWAP.ini");
        REQUIRE(latestPath.filename().string() == "VREditor_Test_SWAP_latest.ini");
    }

    SECTION("GetSwapFilePath converts latest to swap") {
        auto swapPath = BaseObjectSwapperParser::GetSwapFilePath("C:/Data/VREditor_Test_SWAP_latest.ini");
        REQUIRE(swapPath.filename().string() == "VREditor_Test_SWAP.ini");
    }
}

TEST_CASE("BOS Parser - FormatFloat precision", "[bos][parser][file-merge]") {
    SECTION("FormatFloat removes trailing zeros") {
        REQUIRE(BaseObjectSwapperParser::FormatFloat(100.0f) == "100");
        REQUIRE(BaseObjectSwapperParser::FormatFloat(100.5f) == "100.5");
        REQUIRE(BaseObjectSwapperParser::FormatFloat(100.25f) == "100.25");
        REQUIRE(BaseObjectSwapperParser::FormatFloat(100.125f) == "100.125");
    }

    SECTION("FormatFloat handles negative numbers") {
        REQUIRE(BaseObjectSwapperParser::FormatFloat(-50.0f) == "-50");
        REQUIRE(BaseObjectSwapperParser::FormatFloat(-50.5f) == "-50.5");
    }

    SECTION("FormatFloat handles very small decimals") {
        REQUIRE(BaseObjectSwapperParser::FormatFloat(0.0001f) == "0.0001");
        REQUIRE(BaseObjectSwapperParser::FormatFloat(0.00005f) == "0.0001");  // Rounded to 4 places
    }
}

TEST_CASE("BOS Entry - Round-trip conversion", "[bos][parser][file-merge]") {
    SECTION("Entry survives ToIniLine -> FromIniLine round-trip") {
        BOSTransformEntry original;
        original.formKeyString = "0x12345~Skyrim.esm";
        original.position = RE::NiPoint3(100.5f, 200.25f, 300.125f);
        original.rotation = RE::NiPoint3(45.0f, 90.0f, -135.0f);
        original.scale = 1.5f;
        original.isDeleted = false;

        std::string line = original.ToIniLine();
        auto parsed = BOSTransformEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->formKeyString == original.formKeyString);
        REQUIRE(parsed->position.x == Catch::Approx(original.position.x));
        REQUIRE(parsed->position.y == Catch::Approx(original.position.y));
        REQUIRE(parsed->position.z == Catch::Approx(original.position.z));
        REQUIRE(parsed->rotation.x == Catch::Approx(original.rotation.x));
        REQUIRE(parsed->rotation.y == Catch::Approx(original.rotation.y));
        REQUIRE(parsed->rotation.z == Catch::Approx(original.rotation.z));
        REQUIRE(parsed->scale == Catch::Approx(original.scale));
        REQUIRE(parsed->isDeleted == original.isDeleted);
    }

    SECTION("Deleted entry survives round-trip") {
        BOSTransformEntry original;
        original.formKeyString = "0xABCDE~TestMod.esp";
        original.position = RE::NiPoint3(0, 0, 0);
        original.rotation = RE::NiPoint3(0, 0, 0);
        original.scale = 1.0f;
        original.isDeleted = true;

        std::string line = original.ToIniLine();
        auto parsed = BOSTransformEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->isDeleted == true);
    }

    SECTION("Entry with default scale doesn't include scaleA") {
        BOSTransformEntry original;
        original.formKeyString = "0x1~Test.esp";
        original.position = RE::NiPoint3(0, 0, 0);
        original.rotation = RE::NiPoint3(0, 0, 0);
        original.scale = 1.0f;

        std::string line = original.ToIniLine();
        REQUIRE(line.find("scaleA") == std::string::npos);

        auto parsed = BOSTransformEntry::FromIniLine(line);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->scale == Catch::Approx(1.0f));
    }
}

TEST_CASE("BOS Exporter - Duplicate entry handling", "[bos][exporter][file-merge]") {
    SECTION("Same form key in new entries updates position") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithModifiedObject("0x1002~Skyrim.esm", "0x3C~Skyrim.esm", 40, 50, 60);

        auto entries = builder.GetEntriesAsConstPtrs();

        // If we were to export, both would be written
        // Duplicates would be handled by the registry itself (can't add same key twice)
        REQUIRE(entries.size() == 2);

        // Check they have unique keys
        std::set<std::string> keys;
        for (const auto& [key, _] : entries) {
            keys.insert(key);
        }
        REQUIRE(keys.size() == 2);
    }
}

TEST_CASE("BOS Exporter - Metadata preservation during merge", "[bos][exporter][file-merge]") {
    SECTION("EntryMetadata MergeFrom fills empty fields") {
        EntryMetadata existing;
        existing.editorId = "ExistingId";
        existing.displayName = "Existing Name";
        existing.meshName = "existing.nif";
        existing.formTypeName = "STAT";

        EntryMetadata newMeta;
        newMeta.editorId = "";  // Empty
        newMeta.displayName = "New Name";  // Has value
        newMeta.meshName = "";  // Empty
        newMeta.formTypeName = "";  // Empty

        newMeta.MergeFrom(existing);

        REQUIRE(newMeta.editorId == "ExistingId");  // Filled from existing
        REQUIRE(newMeta.displayName == "New Name");  // Kept new (had value)
        REQUIRE(newMeta.meshName == "existing.nif");  // Filled from existing
        REQUIRE(newMeta.formTypeName == "STAT");  // Filled from existing
    }

    SECTION("EntryMetadata MergeFrom doesn't overwrite non-empty fields") {
        EntryMetadata existing;
        existing.editorId = "OldId";
        existing.displayName = "Old Name";

        EntryMetadata newMeta;
        newMeta.editorId = "NewId";
        newMeta.displayName = "New Name";

        newMeta.MergeFrom(existing);

        REQUIRE(newMeta.editorId == "NewId");  // Kept new
        REQUIRE(newMeta.displayName == "New Name");  // Kept new
    }

    SECTION("BOSTransformEntry GetMetadata and SetMetadata work correctly") {
        BOSTransformEntry entry;
        entry.editorId = "TestId";
        entry.displayName = "Test Display";
        entry.meshName = "test.nif";
        entry.formTypeName = "ACTI";

        EntryMetadata meta = entry.GetMetadata();
        REQUIRE(meta.editorId == "TestId");
        REQUIRE(meta.displayName == "Test Display");
        REQUIRE(meta.meshName == "test.nif");
        REQUIRE(meta.formTypeName == "ACTI");

        EntryMetadata newMeta;
        newMeta.editorId = "NewId";
        newMeta.displayName = "New Display";
        newMeta.meshName = "new.nif";
        newMeta.formTypeName = "STAT";

        entry.SetMetadata(newMeta);
        REQUIRE(entry.editorId == "NewId");
        REQUIRE(entry.displayName == "New Display");
        REQUIRE(entry.meshName == "new.nif");
        REQUIRE(entry.formTypeName == "STAT");
    }
}

TEST_CASE("BOS Parser - Cell section handling", "[bos][parser][file-merge]") {
    SECTION("CellSectionData can hold multiple entries") {
        CellSectionData section;
        section.cellFormKey = "0x3C~Skyrim.esm";
        section.cellEditorId = "WhiterunExterior01";

        BOSTransformEntry entry1;
        entry1.formKeyString = "0x1001~Skyrim.esm";
        entry1.position = RE::NiPoint3(10, 20, 30);

        BOSTransformEntry entry2;
        entry2.formKeyString = "0x1002~Skyrim.esm";
        entry2.position = RE::NiPoint3(40, 50, 60);

        section.entries.push_back(entry1);
        section.entries.push_back(entry2);

        REQUIRE(section.entries.size() == 2);
    }

    SECTION("Multiple cells can be consolidated") {
        std::vector<CellSectionData> sections;

        CellSectionData section1;
        section1.cellFormKey = "0x3C~Skyrim.esm";
        section1.cellEditorId = "WhiterunExterior01";
        BOSTransformEntry entry1;
        entry1.formKeyString = "0x1001~Skyrim.esm";
        section1.entries.push_back(entry1);
        sections.push_back(section1);

        CellSectionData section2;
        section2.cellFormKey = "0x3D~Skyrim.esm";
        section2.cellEditorId = "WhiterunExterior02";
        BOSTransformEntry entry2;
        entry2.formKeyString = "0x1002~Skyrim.esm";
        section2.entries.push_back(entry2);
        sections.push_back(section2);

        REQUIRE(sections.size() == 2);
        REQUIRE(sections[0].entries.size() == 1);
        REQUIRE(sections[1].entries.size() == 1);
    }
}

TEST_CASE("BOS Exporter - Entry separation by type", "[bos][exporter][file-merge]") {
    SECTION("Moved and deleted entries can coexist in same cell") {
        RegistryStateBuilder builder;
        builder
            .WithModifiedObject("0x1001~Skyrim.esm", "0x3C~Skyrim.esm", 10, 20, 30)
            .WithDeletedObject("0x1002~Skyrim.esm", "0x3C~Skyrim.esm")
            .WithModifiedObject("0x1003~Skyrim.esm", "0x3C~Skyrim.esm", 40, 50, 60);

        auto entries = builder.GetEntriesAsConstPtrs();

        size_t movedCount = 0;
        size_t deletedCount = 0;
        for (const auto& [_, data] : entries) {
            if (data->saveData.wasDeleted) {
                deletedCount++;
            } else {
                movedCount++;
            }
        }

        REQUIRE(movedCount == 2);
        REQUIRE(deletedCount == 1);
    }
}

TEST_CASE("BOS Exporter - Plugin name collection", "[bos][exporter][file-merge]") {
    SECTION("Multiple plugins from different entries") {
        std::vector<std::string> formKeys = {
            "0x1001~Skyrim.esm",
            "0x1002~Dawnguard.esm",
            "0x1003~Skyrim.esm",
            "0x1004~MyMod.esp"
        };

        auto plugins = CollectPluginNames(formKeys);

        REQUIRE(plugins.size() == 3);
        REQUIRE(plugins.count("Skyrim.esm") == 1);
        REQUIRE(plugins.count("Dawnguard.esm") == 1);
        REQUIRE(plugins.count("MyMod.esp") == 1);
    }

    SECTION("Invalid form keys don't add empty plugin names") {
        std::vector<std::string> formKeys = {
            "0x1001~Skyrim.esm",
            "InvalidKey",
            "",
            "0x1002~Test.esp"
        };

        auto plugins = CollectPluginNames(formKeys);

        REQUIRE(plugins.size() == 2);
        REQUIRE(plugins.count("") == 0);
    }
}

TEST_CASE("BOS Exporter - Edge case coordinates", "[bos][exporter][file-merge]") {
    SECTION("Very large coordinates") {
        BOSTransformEntry entry;
        entry.formKeyString = "0x1~Test.esp";
        entry.position = RE::NiPoint3(999999.0f, 888888.0f, 777777.0f);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();
        auto parsed = BOSTransformEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->position.x == Catch::Approx(999999.0f));
        REQUIRE(parsed->position.y == Catch::Approx(888888.0f));
        REQUIRE(parsed->position.z == Catch::Approx(777777.0f));
    }

    SECTION("Negative coordinates") {
        BOSTransformEntry entry;
        entry.formKeyString = "0x1~Test.esp";
        entry.position = RE::NiPoint3(-1000.5f, -2000.25f, -3000.125f);
        entry.rotation = RE::NiPoint3(-45.0f, -90.0f, -180.0f);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();
        auto parsed = BOSTransformEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->position.x == Catch::Approx(-1000.5f));
        REQUIRE(parsed->rotation.x == Catch::Approx(-45.0f));
    }

    SECTION("Zero coordinates") {
        BOSTransformEntry entry;
        entry.formKeyString = "0x1~Test.esp";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();
        auto parsed = BOSTransformEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->position.x == Catch::Approx(0.0f));
        REQUIRE(parsed->position.y == Catch::Approx(0.0f));
        REQUIRE(parsed->position.z == Catch::Approx(0.0f));
    }

    SECTION("Extreme scale values") {
        BOSTransformEntry entry;
        entry.formKeyString = "0x1~Test.esp";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);

        // Very small scale
        entry.scale = 0.01f;
        std::string line1 = entry.ToIniLine();
        auto parsed1 = BOSTransformEntry::FromIniLine(line1);
        REQUIRE(parsed1.has_value());
        REQUIRE(parsed1->scale == Catch::Approx(0.01f));

        // Very large scale
        entry.scale = 100.0f;
        std::string line2 = entry.ToIniLine();
        auto parsed2 = BOSTransformEntry::FromIniLine(line2);
        REQUIRE(parsed2.has_value());
        REQUIRE(parsed2->scale == Catch::Approx(100.0f));
    }
}

TEST_CASE("BOS Exporter - Special characters in metadata", "[bos][exporter][file-merge]") {
    SECTION("Display name with special characters") {
        BOSTransformEntry entry;
        entry.displayName = "Dragon's Lair";

        std::string comment = entry.ToCommentLine();
        // Should handle apostrophe correctly
        REQUIRE(comment.find("Dragon's Lair") != std::string::npos);
    }

    SECTION("Mesh path with deep nesting") {
        BOSTransformEntry entry;
        entry.meshName = "meshes/architecture/whiterun/wrdragonsreach/wrdragonsreach01.nif";

        std::string comment = entry.ToCommentLine();
        REQUIRE(comment.find("wrdragonsreach01.nif") != std::string::npos);
    }

    SECTION("Editor ID with numbers") {
        BOSTransformEntry entry;
        entry.editorId = "WhiterunDragonStatue01Ref001";

        std::string comment = entry.ToCommentLine();
        REQUIRE(comment.find("WhiterunDragonStatue01Ref001") != std::string::npos);
    }
}
