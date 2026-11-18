


#include <catch2/catch_test_macros.hpp>

#include "cppconfig/io/bin.hpp"

#include <array>
#include <iterator>


TEST_CASE("can read/write", "[cppconfig]")
{
    using MyDict = cppdict::Dict<int, double, std::string, std::vector<int>>;

    std::string const file("outfile.dat");

    {
        MyDict md;
        md["PI"] = 3.14;
        md["test"]["super"] = 2;
        md["Key"] = std::string { "Value" };
        md["key2"] = 2;
        md["vec"] = std::vector<int> { 1, 2, 3 };

        cppconfig::config_binary::save_config(file, md);
    }

    auto const md = cppconfig::config_binary::load_config<MyDict>(file);

    SECTION("A dict")
    {
        REQUIRE(md["Key"].to<std::string>() == "Value");
        REQUIRE(md["key2"].to<int>() == 2);
        REQUIRE(md["PI"].to<double>() == 3.14);
        REQUIRE(md["test"]["super"].to<int>() == 2);
        REQUIRE(md["vec"].to<std::vector<int>>() == std::vector<int> { 1, 2, 3 });
    }
}

struct CustomSerializable
{
    int a = 111;
    double b = 333;

    bool operator==(CustomSerializable const& cs) const { return a == cs.a and b == cs.b; }
};


template <typename... Args>
void serialize(cppdict::Dict<Args...>& dict, CustomSerializable const& cm)
{
    dict["cm.a"] = cm.a;
    dict["cm.b"] = cm.b;
}

template <typename... Args>
void deserialize(cppdict::Dict<Args...> const& dict, CustomSerializable& cm)
{
    cm.a = dict["cm.a"].template to<int>();
    cm.b = dict["cm.b"].template to<double>();
}


TEST_CASE("can read/write custom serializable", "[cppconfig]")
{
    using MyDict = cppdict::Dict<int, double, std::string, CustomSerializable>;
    static_assert(cppconfig::config_binary::IsCustomSerializable<MyDict, CustomSerializable>);

    CustomSerializable const cs {};

    {
        std::string const file("outfile.0.dat");

        {
            MyDict md;
            md["key"] = std::string { "Value" };
            md["custom"]["data"]["cm.a"] = cs.a;
            md["custom"]["data"]["cm.b"] = cs.b;
            md["custom"]["other"] = 3.3;
            md["key2"] = 2;
            static_assert(cppconfig::config_binary::HasSerialize<MyDict, CustomSerializable>);

            cppconfig::config_binary::save_config(file, md);
        }

        auto const md = cppconfig::config_binary::load_config<MyDict>(file);

        SECTION("A dict")
        {
            REQUIRE(md["key"].to<std::string>() == "Value");
            REQUIRE(md["custom"]["data"]["cm.a"].to<int>() == cs.a);
            REQUIRE(md["custom"]["data"]["cm.b"].to<double>() == cs.b);
            REQUIRE(md["custom"]["other"].to<double>() == 3.3);
            REQUIRE(md["key2"].to<int>() == 2);
        }
    }

    {
        std::string const file("outfile.1.dat");

        {
            MyDict md;
            md["key"] = std::string { "Value" };
            md["custom"]["data"] = cs;
            md["custom"]["other"] = 3.3;
            md["key2"] = 2;
            static_assert(cppconfig::config_binary::HasSerialize<MyDict, CustomSerializable>);

            cppconfig::config_binary::save_config(file, md);
        }

        auto const md = cppconfig::config_binary::load_config<MyDict>(file);

        SECTION("B dict - direct custom type")
        {
            REQUIRE(md["key"].to<std::string>() == "Value");
            REQUIRE(md["custom"]["other"].to<double>() == 3.3);
            REQUIRE(md["custom"]["data"].to<CustomSerializable>() == cs);
            REQUIRE(md["key2"].to<int>() == 2);
        }
    }
}


TEST_CASE("binary errors fail loudly", "[cppconfig]")
{
    using MyDict = cppdict::Dict<int, double, std::string>;
    using namespace cppconfig::config_binary;

    SECTION("missing file")
    {
        REQUIRE_THROWS(load_config<MyDict>("does/not/exist.dat"));
    }

    SECTION("empty leaf")
    {
        MyDict md;
        md["a"]["b"];
        REQUIRE_THROWS(save_config("outfile.empty_leaf.dat", md));
    }

    SECTION("empty config round trips")
    {
        save_config("outfile.empty.dat", MyDict {});
        REQUIRE(load_config<MyDict>("outfile.empty.dat").isEmpty());
    }

    SECTION("empty map round trips as a map")
    {
        MyDict md;
        md["a"]["b"].data = MyDict::node_t {};
        md["c"] = 1;
        save_config("outfile.empty_map.dat", md);
        auto const loaded = load_config<MyDict>("outfile.empty_map.dat");
        REQUIRE(loaded["a"]["b"].isNode());
        REQUIRE(loaded["c"].to<int>() == 1);
    }

    SECTION("std::array round trips, length mismatch throws")
    {
        using ArrDict = cppdict::Dict<int, std::array<double, 3>>;
        using ArrDict2 = cppdict::Dict<int, std::array<double, 2>>;
        ArrDict ad;
        ad["arr"] = std::array<double, 3> { 1, 2, 3 };
        save_config("outfile.array.dat", ad);
        REQUIRE(load_config<ArrDict>("outfile.array.dat")["arr"].to<std::array<double, 3>>()
            == std::array<double, 3> { 1, 2, 3 });
        // same tag index, different extent
        REQUIRE_THROWS(load_config<ArrDict2>("outfile.array.dat"));
    }

    SECTION("truncated file")
    {
        std::string const file("outfile.truncated.dat");
        MyDict md;
        md["key"] = std::string { "Value" };
        save_config(file, md);
        {
            std::ifstream in { file, std::ios::binary };
            std::string bytes { std::istreambuf_iterator<char>(in), {} };
            std::ofstream { file, std::ios::binary }.write(bytes.data(), bytes.size() - 2);
        }
        REQUIRE_THROWS(load_config<MyDict>(file));
    }
}
