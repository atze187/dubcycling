#include <catch2/catch_test_macros.hpp>
#include <string>

#include "engine/Version.h"

TEST_CASE("engine library links") {
    REQUIRE(std::string(dc::version()) == "0.3.0");
}
