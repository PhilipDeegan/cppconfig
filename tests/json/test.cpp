
#include "cppconfig/cppconfig.hpp"

#include <catch2/catch_test_macros.hpp>


const auto JSON = std::string(R"(
{
  "section1": {
    "key_string": "a string",
    "key_bool": true,
    "key_int": 10,
    "key_float": 1.11111,
    "key_dict": {
      "key": "value"
    }
  },
  "section2": {
    "key_section2": "value"
  }
}
)");

TEST_CASE("JSON to Dict")
{
    auto config = cppconfig::from_json(JSON);
    REQUIRE(config["section1"]["key_string"].to<std::string>("") == "a string");
    REQUIRE(config["section1"]["key_bool"].to<bool>(false) == true);
    REQUIRE(config["section1"]["key_int"].to<int>(0) == 10);
    REQUIRE(config["section1"]["key_float"].to<double>(0.) == 1.11111);
    REQUIRE(config["section1"]["key_dict"]["key"].to<std::string>("") == "value");
    REQUIRE(config["section2"]["key_section2"].to<std::string>("") == "value");
}

TEST_CASE("Dict to JSON")
{
    cppconfig::Config c;
    c["section1"] = 10;
    c["section2"]["field"] = std::string { "value" };
    c["section3"] = 1.5;
    c["section4"] = true;
    auto c2 = cppconfig::from_json(cppconfig::to_json(c));
    REQUIRE(c2["section1"].to<int>(0) == c["section1"].to<int>(-1));
    REQUIRE(
        c["section2"]["field"].to<std::string>("") == c["section2"]["field"].to<std::string>("~"));
    REQUIRE(c2["section3"].to<double>(0) == c["section3"].to<double>(-1));
    REQUIRE(c2["section4"].to<bool>(true) == c["section4"].to<bool>(false));
}
