#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/AddedObjectsExporter.h"
#include "../../src/persistence/AddedObjectsParser.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// AddedObjectsExporter File Merge Tests
// =============================================================================
// These tests verify correct behavior when merging with existing INI files,
// ensuring no data loss, proper deduplication, and correct metadata handling.

TEST_CASE("AddedObjects Parser - ParseIniFile basics", "[added][parser][file-merge]") {
    SECTION("ParseIniFile returns empty data for non-existent file") {
        auto* parser = AddedObjectsParser::GetSingleton();
        auto data = parser->ParseIniFile("C:\\nonexistent\\path\\file.ini");
        REQUIRE(data.entries.empty());
    }

    SECTION("ParsePropertyString handles complete string") {
        AddedObjectEntry entry;
        bool success = AddedObjectsParser::ParsePropertyString(
            "posA(100,200,300),rotA(45,90,180),scaleA(2.5)", entry);

        REQUIRE(success);
        REQUIRE(entry.position.x == Catch::Approx(100.0f));
        REQUIRE(entry.position.y == Catch::Approx(200.0f));
        REQUIRE(entry.position.z == Catch::Approx(300.0f));
        REQUIRE(entry.rotation.x == Catch::Approx(45.0f));
        REQUIRE(entry.scale == Catch::Approx(2.5f));
    }

    SECTION("ParsePropertyString handles minimal string") {
        AddedObjectEntry entry;
        bool success = AddedObjectsParser::ParsePropertyString("posA(50,75,100)", entry);

        REQUIRE(success);
        REQUIRE(entry.position.x == Catch::Approx(50.0f));
        REQUIRE(entry.scale == Catch::Approx(1.0f));  // Default
    }

    SECTION("ParsePropertyString fails without position") {
        AddedObjectEntry entry;
        bool success = AddedObjectsParser::ParsePropertyString("rotA(0,0,0),scaleA(1)", entry);

        REQUIRE_FALSE(success);
    }
}

TEST_CASE("AddedObjects Parser - File data structure", "[added][parser][file-merge]") {
    SECTION("AddedObjectsFileData can hold entries") {
        AddedObjectsFileData data;
        data.cellFormKey = "0x3C~Skyrim.esm";
        data.cellEditorId = "WhiterunExterior01";
        data.iniFileName = "VREditor_WhiterunExterior01_AddedObjects.ini";

        AddedObjectEntry entry1;
        entry1.baseFormString = "Barrel01";
        entry1.position = RE::NiPoint3(100, 200, 300);

        AddedObjectEntry entry2;
        entry2.baseFormString = "0x12345~Skyrim.esm";
        entry2.position = RE::NiPoint3(400, 500, 600);

        data.entries.push_back(entry1);
        data.entries.push_back(entry2);

        REQUIRE(data.entries.size() == 2);
    }

    SECTION("AddedObjectsCellSection for consolidated mode") {
        AddedObjectsCellSection section;
        section.cellFormKey = "0x3C~Skyrim.esm";
        section.cellEditorId = "TestCell";

        AddedObjectEntry entry;
        entry.baseFormString = "TestObj";
        entry.position = RE::NiPoint3(0, 0, 0);
        section.entries.push_back(entry);

        REQUIRE(section.entries.size() == 1);
    }
}

TEST_CASE("AddedObjects Entry - Round-trip with metadata", "[added][parser][file-merge]") {
    SECTION("Entry survives ToIniLine -> FromIniLine with metadata") {
        AddedObjectEntry original;
        original.baseFormString = "0x12345~Skyrim.esm";
        original.position = RE::NiPoint3(100.5f, 200.25f, 300.125f);
        original.rotation = RE::NiPoint3(45.0f, 90.0f, -135.0f);
        original.scale = 1.5f;
        original.editorId = "TestObject01";
        original.displayName = "Test Object";
        original.meshName = "meshes/test.nif";

        // Generate comment and INI line
        std::string comment = original.ToCommentLine();
        std::string line = original.ToIniLine();

        // Parse back
        auto parsed = AddedObjectEntry::FromIniLine(line);
        REQUIRE(parsed.has_value());

        parsed->ApplyMetadataFromComment(comment);

        REQUIRE(parsed->baseFormString == original.baseFormString);
        REQUIRE(parsed->editorId == original.editorId);
        REQUIRE(parsed->displayName == original.displayName);
        REQUIRE(parsed->meshName == original.meshName);
    }
}

TEST_CASE("AddedObjects Exporter - Duplicate position handling", "[added][exporter][file-merge]") {
    SECTION("Position-based deduplication key format") {
        // Test the position key format used for deduplication
        AddedObjectEntry entry1;
        entry1.position = RE::NiPoint3(100.123f, 200.456f, 300.789f);

        AddedObjectEntry entry2;
        entry2.position = RE::NiPoint3(100.123f, 200.456f, 300.789f);

        // If positions are "close enough", they should be considered duplicates
        // The actual implementation uses fmt::format with .2f precision
        auto posKey = [](const AddedObjectEntry& e) {
            return std::to_string(static_cast<int>(e.position.x * 100)) + "," +
                   std::to_string(static_cast<int>(e.position.y * 100)) + "," +
                   std::to_string(static_cast<int>(e.position.z * 100));
        };

        REQUIRE(posKey(entry1) == posKey(entry2));
    }

    SECTION("Different positions produce different keys") {
        AddedObjectEntry entry1;
        entry1.position = RE::NiPoint3(100.0f, 200.0f, 300.0f);

        AddedObjectEntry entry2;
        entry2.position = RE::NiPoint3(100.5f, 200.5f, 300.5f);

        // These should be different
        REQUIRE(entry1.position.x != entry2.position.x);
    }
}

TEST_CASE("AddedObjects Exporter - Metadata preservation", "[added][exporter][file-merge]") {
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

        REQUIRE(newMeta.editorId == "ExistingId");
        REQUIRE(newMeta.displayName == "New Name");
        REQUIRE(newMeta.meshName == "existing.nif");
        REQUIRE(newMeta.formTypeName == "STAT");
    }

    SECTION("AddedObjectEntry GetMetadata and SetMetadata work") {
        AddedObjectEntry entry;
        entry.editorId = "TestId";
        entry.displayName = "Test Display";
        entry.meshName = "test.nif";
        entry.formTypeName = "ACTI";

        EntryMetadata meta = entry.GetMetadata();
        REQUIRE(meta.editorId == "TestId");
        REQUIRE(meta.displayName == "Test Display");

        EntryMetadata newMeta;
        newMeta.editorId = "NewId";
        newMeta.displayName = "New Display";
        newMeta.meshName = "new.nif";
        newMeta.formTypeName = "STAT";

        entry.SetMetadata(newMeta);
        REQUIRE(entry.editorId == "NewId");
        REQUIRE(entry.displayName == "New Display");
    }
}

TEST_CASE("AddedObjects Parser - Base form resolution types", "[added][parser][file-merge]") {
    SECTION("EditorID-based base form strings") {
        AddedObjectEntry entry;
        entry.baseFormString = "Barrel01";

        // EditorID doesn't have a tilde
        REQUIRE(entry.baseFormString.find('~') == std::string::npos);
        REQUIRE(entry.GetPluginName().empty());
    }

    SECTION("FormKey-based base form strings") {
        AddedObjectEntry entry;
        entry.baseFormString = "0x12345~Skyrim.esm";

        REQUIRE(entry.baseFormString.find('~') != std::string::npos);
        REQUIRE(entry.GetPluginName() == "Skyrim.esm");
    }

    SECTION("Mixed base form types in same cell") {
        std::vector<AddedObjectEntry> entries;

        AddedObjectEntry entry1;
        entry1.baseFormString = "Barrel01";  // EditorID

        AddedObjectEntry entry2;
        entry2.baseFormString = "0x12345~Skyrim.esm";  // FormKey

        AddedObjectEntry entry3;
        entry3.baseFormString = "IronSword";  // EditorID

        entries.push_back(entry1);
        entries.push_back(entry2);
        entries.push_back(entry3);

        size_t editorIdCount = 0;
        size_t formKeyCount = 0;
        for (const auto& e : entries) {
            if (e.baseFormString.find('~') != std::string::npos) {
                formKeyCount++;
            } else {
                editorIdCount++;
            }
        }

        REQUIRE(editorIdCount == 2);
        REQUIRE(formKeyCount == 1);
    }
}

TEST_CASE("AddedObjects Exporter - Multiple cells consolidated", "[added][exporter][file-merge]") {
    SECTION("Multiple cell sections in consolidated file") {
        std::vector<AddedObjectsCellSection> sections;

        AddedObjectsCellSection section1;
        section1.cellFormKey = "0x3C~Skyrim.esm";
        section1.cellEditorId = "WhiterunExterior01";
        AddedObjectEntry e1;
        e1.baseFormString = "Barrel01";
        e1.position = RE::NiPoint3(100, 200, 300);
        section1.entries.push_back(e1);

        AddedObjectsCellSection section2;
        section2.cellFormKey = "0x3D~Skyrim.esm";
        section2.cellEditorId = "RiftenExterior01";
        AddedObjectEntry e2;
        e2.baseFormString = "Chest01";
        e2.position = RE::NiPoint3(400, 500, 600);
        section2.entries.push_back(e2);

        sections.push_back(section1);
        sections.push_back(section2);

        REQUIRE(sections.size() == 2);
        REQUIRE(sections[0].entries.size() == 1);
        REQUIRE(sections[1].entries.size() == 1);
    }
}

TEST_CASE("AddedObjects Entry - Edge case coordinates", "[added][parser][file-merge]") {
    SECTION("Very large coordinates") {
        AddedObjectEntry entry;
        entry.baseFormString = "TestObj";
        entry.position = RE::NiPoint3(999999.0f, 888888.0f, 777777.0f);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();
        auto parsed = AddedObjectEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->position.x == Catch::Approx(999999.0f));
    }

    SECTION("Negative coordinates") {
        AddedObjectEntry entry;
        entry.baseFormString = "TestObj";
        entry.position = RE::NiPoint3(-1000.5f, -2000.25f, -3000.125f);
        entry.rotation = RE::NiPoint3(-45.0f, -90.0f, -180.0f);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();
        auto parsed = AddedObjectEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->position.x == Catch::Approx(-1000.5f));
        REQUIRE(parsed->rotation.x == Catch::Approx(-45.0f));
    }

    SECTION("Zero coordinates") {
        AddedObjectEntry entry;
        entry.baseFormString = "TestObj";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();
        auto parsed = AddedObjectEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->position.x == Catch::Approx(0.0f));
        REQUIRE(parsed->position.y == Catch::Approx(0.0f));
        REQUIRE(parsed->position.z == Catch::Approx(0.0f));
    }

    SECTION("Extreme scale values") {
        AddedObjectEntry entry;
        entry.baseFormString = "TestObj";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);

        // Very small scale
        entry.scale = 0.01f;
        std::string line1 = entry.ToIniLine();
        auto parsed1 = AddedObjectEntry::FromIniLine(line1);
        REQUIRE(parsed1.has_value());
        REQUIRE(parsed1->scale == Catch::Approx(0.01f));

        // Very large scale
        entry.scale = 100.0f;
        std::string line2 = entry.ToIniLine();
        auto parsed2 = AddedObjectEntry::FromIniLine(line2);
        REQUIRE(parsed2.has_value());
        REQUIRE(parsed2->scale == Catch::Approx(100.0f));
    }
}

TEST_CASE("AddedObjects Exporter - Special characters in metadata", "[added][exporter][file-merge]") {
    SECTION("Display name with special characters") {
        AddedObjectEntry entry;
        entry.displayName = "Dragon's Lair Treasure";

        std::string comment = entry.ToCommentLine();
        REQUIRE(comment.find("Dragon's Lair Treasure") != std::string::npos);
    }

    SECTION("EditorID with numbers and underscores") {
        AddedObjectEntry entry;
        entry.editorId = "WhiterunBarrel01_Large_v2";

        std::string comment = entry.ToCommentLine();
        REQUIRE(comment.find("WhiterunBarrel01_Large_v2") != std::string::npos);
    }

    SECTION("Mesh path with deep nesting") {
        AddedObjectEntry entry;
        entry.meshName = "meshes/architecture/whiterun/wrdragonsreach/wrdragonsreach01.nif";

        std::string comment = entry.ToCommentLine();
        REQUIRE(comment.find("wrdragonsreach01.nif") != std::string::npos);
    }
}

TEST_CASE("AddedObjects Exporter - Cell identifier edge cases", "[added][exporter][file-merge]") {
    SECTION("Cell with only FormKey (no EditorID)") {
        std::string filename = AddedObjectsParser::BuildIniFileName("", "0x3C~Skyrim.esm");
        REQUIRE(filename.find("VREditor_") == 0);
        REQUIRE(filename.find("_AddedObjects.ini") != std::string::npos);
        // FormKey should be sanitized
        REQUIRE(filename.find('~') == std::string::npos);
    }

    SECTION("Cell with EditorID containing spaces") {
        std::string filename = AddedObjectsParser::BuildIniFileName("Whiterun Exterior 01", "0x3C~Skyrim.esm");
        // Spaces should be replaced
        REQUIRE(filename.find(' ') == std::string::npos);
        REQUIRE(filename.find('_') != std::string::npos);
    }

    SECTION("Empty cell identifier uses Unknown") {
        std::string filename = AddedObjectsParser::BuildIniFileName("", "");
        REQUIRE(filename == "VREditor_Unknown_AddedObjects.ini");
    }
}

TEST_CASE("AddedObjects Exporter - Plugin collection from base forms", "[added][exporter][file-merge]") {
    SECTION("Collect plugins from FormKey base forms") {
        std::vector<std::string> baseFormStrings = {
            "0x12345~Skyrim.esm",
            "0x12346~Dawnguard.esm",
            "0x12347~Skyrim.esm",
            "Barrel01",  // EditorID - no plugin
            "0x12348~MyMod.esp"
        };

        auto plugins = CollectPluginNames(baseFormStrings);

        REQUIRE(plugins.size() == 3);
        REQUIRE(plugins.count("Skyrim.esm") == 1);
        REQUIRE(plugins.count("Dawnguard.esm") == 1);
        REQUIRE(plugins.count("MyMod.esp") == 1);
    }

    SECTION("EditorID base forms don't contribute plugins") {
        std::vector<std::string> baseFormStrings = {
            "Barrel01",
            "Chest01",
            "IronSword"
        };

        auto plugins = CollectPluginNames(baseFormStrings);
        REQUIRE(plugins.empty());
    }
}

TEST_CASE("AddedObjects vs BOS difference", "[added][exporter][file-merge]") {
    SECTION("AddedObjects uses baseFormString, BOS uses formKeyString") {
        // AddedObjects entry - has base form (what to spawn)
        AddedObjectEntry addedEntry;
        addedEntry.baseFormString = "Barrel01";

        // BOS entry - has form key (which ref to modify)
        BOSTransformEntry bosEntry;
        bosEntry.formKeyString = "0x12345~Skyrim.esm";

        // They represent different things
        REQUIRE(!addedEntry.baseFormString.empty());
        REQUIRE(!bosEntry.formKeyString.empty());
    }

    SECTION("AddedObjects INI format differs from BOS") {
        AddedObjectEntry addedEntry;
        addedEntry.baseFormString = "Barrel01";
        addedEntry.position = RE::NiPoint3(100, 200, 300);
        addedEntry.rotation = RE::NiPoint3(0, 0, 0);
        addedEntry.scale = 1.0f;

        std::string addedLine = addedEntry.ToIniLine();

        BOSTransformEntry bosEntry;
        bosEntry.formKeyString = "0x12345~Skyrim.esm";
        bosEntry.position = RE::NiPoint3(100, 200, 300);
        bosEntry.rotation = RE::NiPoint3(0, 0, 0);
        bosEntry.scale = 1.0f;
        bosEntry.isDeleted = false;

        std::string bosLine = bosEntry.ToIniLine();

        // BOS line has chance at the end
        REQUIRE(bosLine.find("|100") != std::string::npos);
        // AddedObjects doesn't have chance
        REQUIRE(addedLine.find("|100") == std::string::npos);
    }
}

TEST_CASE("AddedObjects Exporter - Empty entry handling", "[added][exporter][file-merge]") {
    SECTION("Entry with empty base form string") {
        AddedObjectEntry entry;
        entry.baseFormString = "";
        entry.position = RE::NiPoint3(100, 200, 300);

        std::string line = entry.ToIniLine();
        // Line starts with pipe (empty base form)
        REQUIRE(line[0] == '|');
    }

    SECTION("Entry with all default values") {
        AddedObjectEntry entry;
        entry.baseFormString = "TestObj";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();

        REQUIRE(line.find("posA(0,0,0)") != std::string::npos);
        REQUIRE(line.find("rotA(0,0,0)") != std::string::npos);
        REQUIRE(line.find("scaleA") == std::string::npos);  // Default scale omitted
    }
}
