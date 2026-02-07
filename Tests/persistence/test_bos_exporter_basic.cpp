#include <catch2/catch_all.hpp>
#include "TestHelpers.h"
#include "../../src/persistence/BaseObjectSwapperExporter.h"
#include "../../src/persistence/BaseObjectSwapperParser.h"

using namespace TestHelpers;
using namespace Persistence;

// =============================================================================
// BaseObjectSwapperExporter Basic Tests
// =============================================================================
// These tests verify fundamental export behavior with straightforward inputs.

TEST_CASE("BOS Exporter - Empty inputs", "[bos][exporter][basic]") {
    SECTION("ExportEntries with empty vector returns 0") {
        auto* exporter = BaseObjectSwapperExporter::GetSingleton();
        std::vector<std::pair<std::string, const ChangedObjectRuntimeData*>> entries;

        size_t result = exporter->ExportEntries(entries);

        REQUIRE(result == 0);
    }

    SECTION("ExportPendingChanges with no pending entries returns 0") {
        ChangedObjectRegistry registry;
        auto* exporter = BaseObjectSwapperExporter::GetSingleton();

        size_t result = exporter->ExportPendingChanges(registry);

        REQUIRE(result == 0);
        REQUIRE(registry.Count() == 0);
    }
}

TEST_CASE("BOS Exporter - TransformToBOSEntry conversion", "[bos][exporter][basic]") {
    SECTION("Basic transform conversion produces valid entry") {
        std::string formKey = "0x12345~Skyrim.esm";
        RE::NiTransform transform = MakeTransform(100.0f, 200.0f, 300.0f, 1.5f);

        BOSTransformEntry entry = BaseObjectSwapperExporter::TransformToBOSEntry(
            formKey, transform, false);

        REQUIRE(entry.formKeyString == formKey);
        // Note: Without actual game forms loaded, position comes from stored transform
        REQUIRE(entry.scale == 1.5f);
        REQUIRE(entry.isDeleted == false);
    }

    SECTION("Deleted entry has isDeleted flag set") {
        std::string formKey = "0x12345~Skyrim.esm";
        RE::NiTransform transform = MakeTransform(0, 0, 0);

        BOSTransformEntry entry = BaseObjectSwapperExporter::TransformToBOSEntry(
            formKey, transform, true);

        REQUIRE(entry.isDeleted == true);
    }

    SECTION("Entry ToIniLine produces valid format") {
        BOSTransformEntry entry;
        entry.formKeyString = "0xABCDE~TestMod.esp";
        entry.position = RE::NiPoint3(100.0f, 200.0f, 300.0f);
        entry.rotation = RE::NiPoint3(0, 45.0f, 90.0f);
        entry.scale = 1.0f;
        entry.isDeleted = false;

        std::string line = entry.ToIniLine();

        REQUIRE(line.find("0xABCDE~TestMod.esp|") == 0);
        REQUIRE(line.find("posA(100,200,300)") != std::string::npos);
        REQUIRE(line.find("rotA(0,45,90)") != std::string::npos);
        REQUIRE(line.find("|100") != std::string::npos);  // Chance
        // Scale 1.0 should be omitted
        REQUIRE(line.find("scaleA") == std::string::npos);
    }

    SECTION("Entry ToIniLine includes scale when not 1.0") {
        BOSTransformEntry entry;
        entry.formKeyString = "0x1~Test.esp";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 2.5f;
        entry.isDeleted = false;

        std::string line = entry.ToIniLine();

        REQUIRE(line.find("scaleA(2.5)") != std::string::npos);
    }

    SECTION("Deleted entry ToIniLine includes Initially Disabled flag") {
        BOSTransformEntry entry;
        entry.formKeyString = "0x1~Test.esp";
        entry.position = RE::NiPoint3(0, 0, 0);
        entry.rotation = RE::NiPoint3(0, 0, 0);
        entry.scale = 1.0f;
        entry.isDeleted = true;

        std::string line = entry.ToIniLine();

        REQUIRE(line.find("flags(0x00000800)") != std::string::npos);
    }
}

TEST_CASE("BOS Exporter - Angle normalization", "[bos][exporter][basic]") {
    SECTION("NormalizeAngleDegrees keeps angles in -180 to +180 range") {
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(0.0f) == Catch::Approx(0.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(90.0f) == Catch::Approx(90.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(-90.0f) == Catch::Approx(-90.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(180.0f) == Catch::Approx(180.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(-180.0f) == Catch::Approx(-180.0f));
    }

    SECTION("NormalizeAngleDegrees wraps angles above 180") {
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(270.0f) == Catch::Approx(-90.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(360.0f) == Catch::Approx(0.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(450.0f) == Catch::Approx(90.0f));
    }

    SECTION("NormalizeAngleDegrees wraps angles below -180") {
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(-270.0f) == Catch::Approx(90.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(-360.0f) == Catch::Approx(0.0f));
        REQUIRE(BaseObjectSwapperExporter::NormalizeAngleDegrees(-450.0f) == Catch::Approx(-90.0f));
    }
}

TEST_CASE("BOS Exporter - Plugin name extraction", "[bos][exporter][basic]") {
    SECTION("GetPluginName extracts plugin from formKeyString") {
        BOSTransformEntry entry;
        entry.formKeyString = "0x12345~Skyrim.esm";
        REQUIRE(entry.GetPluginName() == "Skyrim.esm");

        entry.formKeyString = "0xABCDE~MyMod.esp";
        REQUIRE(entry.GetPluginName() == "MyMod.esp");

        entry.formKeyString = "0x1~Test Plugin Name.esl";
        REQUIRE(entry.GetPluginName() == "Test Plugin Name.esl");
    }

    SECTION("GetPluginName returns empty for invalid formKeyString") {
        BOSTransformEntry entry;
        entry.formKeyString = "InvalidFormKey";
        REQUIRE(entry.GetPluginName().empty());

        entry.formKeyString = "";
        REQUIRE(entry.GetPluginName().empty());
    }
}

TEST_CASE("BOS Exporter - Entry comment lines", "[bos][exporter][basic]") {
    SECTION("ToCommentLine generates pipe-separated format") {
        BOSTransformEntry entry;
        entry.editorId = "TestObject01";
        entry.displayName = "Test Object";
        entry.meshName = "meshes/test/object.nif";

        std::string comment = entry.ToCommentLine();

        REQUIRE(comment.starts_with("; "));
        REQUIRE(comment.find("TestObject01") != std::string::npos);
        REQUIRE(comment.find("Test Object") != std::string::npos);
        REQUIRE(comment.find("meshes/test/object.nif") != std::string::npos);
        REQUIRE(comment.find('|') != std::string::npos);
    }

    SECTION("ToCommentLine handles empty fields") {
        BOSTransformEntry entry;
        entry.editorId = "";
        entry.displayName = "";
        entry.meshName = "meshes/test.nif";

        std::string comment = entry.ToCommentLine();

        // Should still have the format but with empty fields
        REQUIRE(comment.starts_with("; "));
        REQUIRE(comment.find("||meshes/test.nif") != std::string::npos);
    }

    SECTION("ApplyMetadataFromComment parses pipe-separated format") {
        BOSTransformEntry entry;
        entry.ApplyMetadataFromComment("; EditorID|Display Name|meshes/path.nif|STAT");

        REQUIRE(entry.editorId == "EditorID");
        REQUIRE(entry.displayName == "Display Name");
        REQUIRE(entry.meshName == "meshes/path.nif");
        REQUIRE(entry.formTypeName == "STAT");
    }

    SECTION("ApplyMetadataFromComment does not overwrite existing data") {
        BOSTransformEntry entry;
        entry.editorId = "ExistingId";
        entry.displayName = "";  // Empty, should be filled
        entry.meshName = "existing.nif";

        entry.ApplyMetadataFromComment("; NewId|New Name|new.nif|ACTI");

        REQUIRE(entry.editorId == "ExistingId");  // Not overwritten
        REQUIRE(entry.displayName == "New Name");  // Filled
        REQUIRE(entry.meshName == "existing.nif");  // Not overwritten
    }
}

TEST_CASE("BOS Exporter - FromIniLine parsing", "[bos][exporter][basic]") {
    SECTION("Parses valid BOS line") {
        std::string line = "0x12345~Skyrim.esm|posA(100,200,300),rotA(0,45,90)|100";

        auto entry = BOSTransformEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->formKeyString == "0x12345~Skyrim.esm");
        REQUIRE(entry->position.x == Catch::Approx(100.0f));
        REQUIRE(entry->position.y == Catch::Approx(200.0f));
        REQUIRE(entry->position.z == Catch::Approx(300.0f));
        REQUIRE(entry->rotation.x == Catch::Approx(0.0f));
        REQUIRE(entry->rotation.y == Catch::Approx(45.0f));
        REQUIRE(entry->rotation.z == Catch::Approx(90.0f));
    }

    SECTION("Parses line with scale") {
        std::string line = "0x1~Test.esp|posA(0,0,0),rotA(0,0,0),scaleA(2.5)|100";

        auto entry = BOSTransformEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->scale == Catch::Approx(2.5f));
    }

    SECTION("Parses line with Initially Disabled flag") {
        std::string line = "0x1~Test.esp|posA(0,0,0),rotA(0,0,0),flags(0x00000800)|100";

        auto entry = BOSTransformEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->isDeleted == true);
    }

    SECTION("Returns nullopt for comment lines") {
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("; This is a comment").has_value());
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("# This is also a comment").has_value());
    }

    SECTION("Returns nullopt for section headers") {
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("[Transforms]").has_value());
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("[Transforms:Cell01]").has_value());
    }

    SECTION("Returns nullopt for empty lines") {
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("").has_value());
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("   ").has_value());
    }

    SECTION("Returns nullopt for malformed lines") {
        // Missing position
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("0x1~Test.esp|rotA(0,0,0)|100").has_value());
        // Missing pipe separator
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("0x1~Test.espposA(0,0,0)").has_value());
        // Just form key
        REQUIRE_FALSE(BOSTransformEntry::FromIniLine("0x1~Test.esp").has_value());
    }

    SECTION("Handles whitespace in form key") {
        std::string line = "  0x12345~Skyrim.esm  |posA(0,0,0),rotA(0,0,0)|100";

        auto entry = BOSTransformEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->formKeyString == "0x12345~Skyrim.esm");
    }

    SECTION("Handles negative coordinates") {
        std::string line = "0x1~Test.esp|posA(-100.5,-200.25,-300.75),rotA(-45,-90,-180)|100";

        auto entry = BOSTransformEntry::FromIniLine(line);

        REQUIRE(entry.has_value());
        REQUIRE(entry->position.x == Catch::Approx(-100.5f));
        REQUIRE(entry->position.y == Catch::Approx(-200.25f));
        REQUIRE(entry->position.z == Catch::Approx(-300.75f));
        REQUIRE(entry->rotation.x == Catch::Approx(-45.0f));
    }
}
