#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/AddedObjectsExporter.h"
#include "../../src/persistence/AddedObjectsParser.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// AddedObjectsExporter Basic Tests
// =============================================================================
// These tests verify fundamental export behavior for created objects.

TEST_CASE("AddedObjects Exporter - Empty inputs", "[added][exporter][basic]") {
    SECTION("ExportEntries with empty vector returns 0") {
        auto* exporter = AddedObjectsExporter::GetSingleton();
        std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>> entries;

        size_t result = exporter->ExportEntries(entries);

        REQUIRE(result == 0);
    }

    SECTION("ExportPendingCreatedObjects with no created objects returns 0") {
        ChangedObjectRegistry registry;
        auto* exporter = AddedObjectsExporter::GetSingleton();

        // Add only modified (non-created) entries
        std::vector<ChangedObjectSaveGameData> saveData;
        saveData.push_back(MakeExistingSaveData("0x1001~Skyrim.esm", "0x3C~Skyrim.esm"));
        registry.LoadEntries(std::move(saveData));

        size_t result = exporter->ExportPendingCreatedObjects(registry);

        REQUIRE(result == 0);
    }
}

TEST_CASE("AddedObjects Entry - ToIniLine format", "[added][exporter][basic]") {
    SECTION("Basic entry produces valid format") {
        AddedObjectEntry entry;
        entry.baseFormString = "Barrel01";  // EditorID
        entry.position = RE::NiPoint3(100.0f, 200.0f, 300.0f);
        entry.rotation = RE::NiPoint3(0, 45.0f, 90.0f);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();

        REQUIRE(line.find("Barrel01|") == 0);
        REQUIRE(line.find("posA(100,200,300)") != std::string::npos);
        REQUIRE(line.find("rotA(0,45,90)") != std::string::npos);
        // Scale 1.0 should be omitted
        REQUIRE(line.find("scaleA") == std::string::npos);
    }

    SECTION("Entry with FormKey as base form") {
        AddedObjectEntry entry;
        entry.baseFormString = "0x12345~Skyrim.esm";  // FormKey
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 1.0f;

        std::string line = entry.ToIniLine();

        REQUIRE(line.find("0x12345~Skyrim.esm|") == 0);
    }

    SECTION("Entry includes scale when not 1.0") {
        AddedObjectEntry entry;
        entry.baseFormString = "TestObject";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 2.5f;

        std::string line = entry.ToIniLine();

        REQUIRE(line.find("scaleA(2.5)") != std::string::npos);
    }
}

TEST_CASE("AddedObjects Entry - FromIniLine parsing", "[added][exporter][basic]") {
    SECTION("Parses valid AddedObjects line with EditorID") {
        std::string line = "Barrel01|posA(100,200,300),rotA(0,45,90)";

        auto entry = AddedObjectEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->baseFormString == "Barrel01");
        REQUIRE(entry->position.x == Catch::Approx(100.0f));
        REQUIRE(entry->position.y == Catch::Approx(200.0f));
        REQUIRE(entry->position.z == Catch::Approx(300.0f));
        REQUIRE(entry->rotation.y == Catch::Approx(45.0f));
        REQUIRE(entry->rotation.z == Catch::Approx(90.0f));
    }

    SECTION("Parses valid AddedObjects line with FormKey") {
        std::string line = "0x12345~Skyrim.esm|posA(50,75,100),rotA(0,0,0)";

        auto entry = AddedObjectEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->baseFormString == "0x12345~Skyrim.esm");
    }

    SECTION("Parses line with scale") {
        std::string line = "TestObj|posA(0,0,0),rotA(0,0,0),scaleA(1.5)";

        auto entry = AddedObjectEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->scale == Catch::Approx(1.5f));
    }

    SECTION("Returns nullopt for comment lines") {
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("; This is a comment").has_value());
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("# Also a comment").has_value());
    }

    SECTION("Returns nullopt for section headers") {
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("[AddedObjects]").has_value());
    }

    SECTION("Returns nullopt for empty lines") {
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("").has_value());
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("   ").has_value());
    }

    SECTION("Returns nullopt for malformed lines") {
        // Missing position
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("Barrel01|rotA(0,0,0)").has_value());
        // Missing pipe separator
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("Barrel01posA(0,0,0)").has_value());
        // Just base form
        REQUIRE_FALSE(AddedObjectEntry::FromIniLine("Barrel01").has_value());
    }

    SECTION("Handles whitespace in base form") {
        std::string line = "  Barrel01  |posA(0,0,0),rotA(0,0,0)";

        auto entry = AddedObjectEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->baseFormString == "Barrel01");
    }

    SECTION("Handles negative coordinates") {
        std::string line = "TestObj|posA(-100.5,-200.25,-300),rotA(-45,-90,-180)";

        auto entry = AddedObjectEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->position.x == Catch::Approx(-100.5f));
        REQUIRE(entry->position.y == Catch::Approx(-200.25f));
        REQUIRE(entry->position.z == Catch::Approx(-300.0f));
    }
}

TEST_CASE("AddedObjects Entry - Comment handling", "[added][exporter][basic]") {
    SECTION("ToCommentLine generates pipe-separated format") {
        AddedObjectEntry entry;
        entry.editorId = "Barrel01";
        entry.displayName = "Barrel";
        entry.meshName = "meshes/clutter/barrel01.nif";

        std::string comment = entry.ToCommentLine();

        REQUIRE(comment.starts_with("; "));
        REQUIRE(comment.find("Barrel01") != std::string::npos);
        REQUIRE(comment.find("Barrel") != std::string::npos);
        REQUIRE(comment.find("barrel01.nif") != std::string::npos);
    }

    SECTION("ToCommentLine handles empty fields") {
        AddedObjectEntry entry;
        entry.editorId = "";
        entry.displayName = "";
        entry.meshName = "meshes/test.nif";

        std::string comment = entry.ToCommentLine();

        REQUIRE(comment.starts_with("; "));
        REQUIRE(comment.find("||meshes/test.nif") != std::string::npos);
    }

    SECTION("ApplyMetadataFromComment parses correctly") {
        AddedObjectEntry entry;
        entry.ApplyMetadataFromComment("; TestId|Test Name|meshes/path.nif|STAT");

        REQUIRE(entry.editorId == "TestId");
        REQUIRE(entry.displayName == "Test Name");
        REQUIRE(entry.meshName == "meshes/path.nif");
        REQUIRE(entry.formTypeName == "STAT");
    }

    SECTION("ApplyMetadataFromComment does not overwrite existing data") {
        AddedObjectEntry entry;
        entry.editorId = "ExistingId";
        entry.displayName = "";  // Empty, should be filled
        entry.meshName = "existing.nif";

        entry.ApplyMetadataFromComment("; NewId|New Name|new.nif|ACTI");

        REQUIRE(entry.editorId == "ExistingId");  // Not overwritten
        REQUIRE(entry.displayName == "New Name");  // Filled
        REQUIRE(entry.meshName == "existing.nif");  // Not overwritten
    }
}

TEST_CASE("AddedObjects Entry - Plugin name extraction", "[added][exporter][basic]") {
    SECTION("GetPluginName extracts from FormKey base form") {
        AddedObjectEntry entry;
        entry.baseFormString = "0x12345~Skyrim.esm";
        REQUIRE(entry.GetPluginName() == "Skyrim.esm");

        entry.baseFormString = "0xABCDE~MyMod.esp";
        REQUIRE(entry.GetPluginName() == "MyMod.esp");
    }

    SECTION("GetPluginName returns empty for EditorID base form") {
        AddedObjectEntry entry;
        entry.baseFormString = "Barrel01";  // EditorID, no plugin
        REQUIRE(entry.GetPluginName().empty());
    }
}

TEST_CASE("AddedObjects Exporter - TransformToEntry conversion", "[added][exporter][basic]") {
    SECTION("TransformToEntry creates valid entry") {
        RE::NiTransform transform = MakeTransform(100.0f, 200.0f, 300.0f, 1.5f);
        std::string baseFormKey = "0x12345~Skyrim.esm";

        AddedObjectEntry entry = AddedObjectsExporter::TransformToEntry(transform, baseFormKey);

        // Without actual game data loaded, baseFormString comes from the key
        REQUIRE(!entry.baseFormString.empty());
        REQUIRE(entry.scale == Catch::Approx(1.5f));
    }
}

TEST_CASE("AddedObjects Parser - FormatFloat", "[added][parser][basic]") {
    SECTION("Removes trailing zeros") {
        REQUIRE(AddedObjectsParser::FormatFloat(100.0f) == "100");
        REQUIRE(AddedObjectsParser::FormatFloat(100.5f) == "100.5");
        REQUIRE(AddedObjectsParser::FormatFloat(100.25f) == "100.25");
    }

    SECTION("Handles negative numbers") {
        REQUIRE(AddedObjectsParser::FormatFloat(-50.0f) == "-50");
        REQUIRE(AddedObjectsParser::FormatFloat(-50.5f) == "-50.5");
    }
}

TEST_CASE("AddedObjects Parser - SanitizeForFilename", "[added][parser][basic]") {
    SECTION("Replaces invalid characters") {
        REQUIRE(AddedObjectsParser::SanitizeForFilename("Test<>:\"/\\|?*~") == "Test");
        REQUIRE(AddedObjectsParser::SanitizeForFilename("Hello World") == "Hello_World");
    }

    SECTION("Handles FormKey format") {
        // FormKey with tilde gets sanitized
        std::string result = AddedObjectsParser::SanitizeForFilename("0x3C~Skyrim.esm");
        REQUIRE(result.find('~') == std::string::npos);
        REQUIRE(result.find('_') != std::string::npos);
    }
}

TEST_CASE("AddedObjects Parser - BuildIniFileName", "[added][parser][basic]") {
    SECTION("Uses EditorID when available") {
        std::string filename = AddedObjectsParser::BuildIniFileName("WhiterunExterior01", "0x3C~Skyrim.esm");
        REQUIRE(filename == "VREditor_WhiterunExterior01_AddedObjects.ini");
    }

    SECTION("Falls back to FormKey when no EditorID") {
        std::string filename = AddedObjectsParser::BuildIniFileName("", "0x3C~Skyrim.esm");
        REQUIRE(filename.find("VREditor_") == 0);
        REQUIRE(filename.find("_AddedObjects.ini") != std::string::npos);
        // FormKey should be sanitized
        REQUIRE(filename.find('~') == std::string::npos);
    }

    SECTION("Uses Unknown when both empty") {
        std::string filename = AddedObjectsParser::BuildIniFileName("", "");
        REQUIRE(filename == "VREditor_Unknown_AddedObjects.ini");
    }
}

TEST_CASE("AddedObjects Entry - Round-trip conversion", "[added][parser][basic]") {
    SECTION("Entry survives ToIniLine -> FromIniLine round-trip") {
        AddedObjectEntry original;
        original.baseFormString = "Barrel01";
        original.position = RE::NiPoint3(100.5f, 200.25f, 300.125f);
        original.rotation = RE::NiPoint3(45.0f, 90.0f, -135.0f);
        original.scale = 1.5f;

        std::string line = original.ToIniLine();
        auto parsed = AddedObjectEntry::FromIniLine(line);

        REQUIRE(parsed.has_value());
        REQUIRE(parsed->baseFormString == original.baseFormString);
        REQUIRE(parsed->position.x == Catch::Approx(original.position.x));
        REQUIRE(parsed->position.y == Catch::Approx(original.position.y));
        REQUIRE(parsed->position.z == Catch::Approx(original.position.z));
        REQUIRE(parsed->rotation.x == Catch::Approx(original.rotation.x));
        REQUIRE(parsed->rotation.y == Catch::Approx(original.rotation.y));
        REQUIRE(parsed->rotation.z == Catch::Approx(original.rotation.z));
        REQUIRE(parsed->scale == Catch::Approx(original.scale));
    }

    SECTION("Entry with default scale doesn't include scaleA") {
        AddedObjectEntry original;
        original.baseFormString = "TestObj";
        original.position = RE::NiPoint3(0, 0, 0);
        original.rotation = RE::NiPoint3(0, 0, 0);
        original.scale = 1.0f;

        std::string line = original.ToIniLine();
        REQUIRE(line.find("scaleA") == std::string::npos);

        auto parsed = AddedObjectEntry::FromIniLine(line);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->scale == Catch::Approx(1.0f));
    }
}

TEST_CASE("AddedObjects Parser - Property string parsing", "[added][parser][basic]") {
    SECTION("ParsePropertyString handles complete string") {
        AddedObjectEntry entry;
        bool success = AddedObjectsParser::ParsePropertyString(
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

    SECTION("ParsePropertyString handles minimal string") {
        AddedObjectEntry entry;
        bool success = AddedObjectsParser::ParsePropertyString("posA(50,75,100)", entry);

        REQUIRE(success);
        REQUIRE(entry.position.x == Catch::Approx(50.0f));
        REQUIRE(entry.rotation.x == Catch::Approx(0.0f));  // Default
        REQUIRE(entry.scale == Catch::Approx(1.0f));  // Default
    }

    SECTION("ParsePropertyString fails without position") {
        AddedObjectEntry entry;
        bool success = AddedObjectsParser::ParsePropertyString("rotA(0,0,0),scaleA(1)", entry);

        REQUIRE_FALSE(success);
    }
}
