
#include "cppconfig/cppconfig.hpp"

#include <catch2/catch_test_macros.hpp>

const auto YML = std::string(R"(
section1:
  key_bool: true
  key_dict:
    key: value
  key_float: 1.11111
  key_int: 10
  key_string: a string
section2:
  key_section2: value
)");

TEST_CASE("YAML to Dict")
{
    auto config = cppconfig::from_yaml(YML);
    REQUIRE(config["section1"]["key_string"].to<std::string>("") == "a string");
    REQUIRE(config["section1"]["key_bool"].to<bool>(false) == true);
    REQUIRE(config["section1"]["key_int"].to<int>(0) == 10);
    REQUIRE(config["section1"]["key_float"].to<double>(0.) == 1.11111);
    REQUIRE(config["section1"]["key_dict"]["key"].to<std::string>("") == "value");
    REQUIRE(config["section2"]["key_section2"].to<std::string>("") == "value");
}
