#include <catch2/catch_all.hpp>

// =============================================================================
// Placeholder test to validate the test infrastructure works
// =============================================================================

TEST_CASE("Test infrastructure works", "[placeholder]") {
    SECTION("Basic assertion") {
        REQUIRE(1 + 1 == 2);
    }

    SECTION("Boolean checks") {
        REQUIRE(true);
        REQUIRE_FALSE(false);
    }
}
