


#include <catch2/catch_test_macros.hpp>

#include "cppconfig/io/cbor.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace cppconfig::config_cbor;


TEST_CASE("can read/write", "[cppconfig]")
{
    using MyDict = cppdict::Dict<int, double, std::string, std::vector<int>>;

    std::string const file("outfile.cbor");

    {
        MyDict md;
        md["PI"] = 3.14;
        md["test"]["super"] = 2;
        md["Key"] = std::string { "Value" };
        md["key2"] = 2;
        md["vec"] = std::vector<int> { 1, 2, 3 };

        save_config(file, md);
    }

    auto const md = load_config<MyDict>(file);

    REQUIRE(md["Key"].to<std::string>() == "Value");
    REQUIRE(md["key2"].to<int>() == 2);
    REQUIRE(md["PI"].to<double>() == 3.14);
    REQUIRE(md["test"]["super"].to<int>() == 2);
    REQUIRE(md["vec"].to<std::vector<int>>() == std::vector<int> { 1, 2, 3 });
}


TEST_CASE("file is plain CBOR", "[cppconfig]")
{
    using MyDict = cppdict::Dict<std::int32_t, double, std::string, std::vector<double>>;
    MyDict md;
    md["a"]["b"] = 1;
    md["s"] = std::string { "x" };
    md["v"] = std::vector<double> { 1.5, -2 };

    auto const doc = json::from_cbor(to_bytes(md));
    REQUIRE(doc["format"] == "cppconfig");
    REQUIRE(doc["version"] == format_version);
    REQUIRE(doc["root"]["a"]["b"] == json::array({ "i32", 1 }));
    REQUIRE(doc["root"]["s"] == json::array({ "str", "x" }));
    REQUIRE(doc["root"]["v"] == json::array({ "vec<f64>", { 1.5, -2.0 } }));
}


TEST_CASE("leaf type order does not matter", "[cppconfig]")
{
    cppdict::Dict<std::int64_t, double, std::string> md;
    md["i"] = std::int64_t { 1 } << 40;
    md["d"] = 0.5;
    md["s"] = std::string { "s" };

    auto const loaded = from_bytes<cppdict::Dict<std::string, double, std::int64_t>>(to_bytes(md));
    REQUIRE(loaded["i"].to<std::int64_t>() == std::int64_t { 1 } << 40);
    REQUIRE(loaded["d"].to<double>() == 0.5);
    REQUIRE(loaded["s"].to<std::string>() == "s");
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
    static_assert(IsCustomSerializable<MyDict, CustomSerializable>);

    CustomSerializable const cs {};

    SECTION("custom type fields written by hand")
    {
        MyDict md;
        md["key"] = std::string { "Value" };
        md["custom"]["data"]["cm.a"] = cs.a;
        md["custom"]["data"]["cm.b"] = cs.b;
        md["custom"]["other"] = 3.3;
        md["key2"] = 2;
        save_config("outfile.0.cbor", md);

        auto const loaded = load_config<MyDict>("outfile.0.cbor");
        REQUIRE(loaded["key"].to<std::string>() == "Value");
        REQUIRE(loaded["custom"]["data"]["cm.a"].to<int>() == cs.a);
        REQUIRE(loaded["custom"]["data"]["cm.b"].to<double>() == cs.b);
        REQUIRE(loaded["custom"]["other"].to<double>() == 3.3);
        REQUIRE(loaded["key2"].to<int>() == 2);
    }

    SECTION("direct custom type")
    {
        MyDict md;
        md["key"] = std::string { "Value" };
        md["custom"]["data"] = cs;
        md["custom"]["other"] = 3.3;
        md["key2"] = 2;
        save_config("outfile.1.cbor", md);

        auto const loaded = load_config<MyDict>("outfile.1.cbor");
        REQUIRE(loaded["key"].to<std::string>() == "Value");
        REQUIRE(loaded["custom"]["other"].to<double>() == 3.3);
        REQUIRE(loaded["custom"]["data"].to<CustomSerializable>() == cs);
        REQUIRE(loaded["key2"].to<int>() == 2);
    }
}


namespace nested
{
struct Inner
{
    std::vector<double> v { 1, 2 };
    bool operator==(Inner const&) const = default;
};

struct Outer
{
    Inner inner { { 3, 4, 5 } };
    std::string name = "outer";
    bool operator==(Outer const&) const = default;
};

inline std::string cppconfig_type_name(std::type_identity<Inner>)
{
    return "Inner";
}
inline std::string cppconfig_type_name(std::type_identity<Outer>)
{
    return "Outer";
}

template <typename... Args>
void serialize(cppdict::Dict<Args...>& dict, Inner const& in)
{
    dict["v"] = in.v;
}
template <typename... Args>
void deserialize(cppdict::Dict<Args...> const& dict, Inner& in)
{
    in.v = dict["v"].template to<std::vector<double>>();
}

template <typename... Args>
void serialize(cppdict::Dict<Args...>& dict, Outer const& out)
{
    dict["inner"] = out.inner;
    dict["meta"]["name"] = out.name;
}
template <typename... Args>
void deserialize(cppdict::Dict<Args...> const& dict, Outer& out)
{
    out.inner = dict["inner"].template to<Inner>();
    out.name = dict["meta"]["name"].template to<std::string>();
}
} // namespace nested


TEST_CASE("custom types nest", "[cppconfig]")
{
    using MyDict
        = cppdict::Dict<int, std::string, std::vector<double>, nested::Inner, nested::Outer>;

    nested::Outer const outer {};
    MyDict md;
    md["outer"] = outer;
    md["inner"] = nested::Inner {};

    auto const bytes = to_bytes(md);
    REQUIRE(json::from_cbor(bytes)["root"]["outer"][0] == "Outer");

    auto const loaded = from_bytes<MyDict>(bytes);
    REQUIRE(loaded["outer"].to<nested::Outer>() == outer);
    REQUIRE(loaded["inner"].to<nested::Inner>() == nested::Inner {});
}


struct Unnamed1
{
};
struct Unnamed2
{
};
template <typename... Args>
void serialize(cppdict::Dict<Args...>&, Unnamed1 const&)
{
}
template <typename... Args>
void deserialize(cppdict::Dict<Args...> const&, Unnamed1&)
{
}
template <typename... Args>
void serialize(cppdict::Dict<Args...>&, Unnamed2 const&)
{
}
template <typename... Args>
void deserialize(cppdict::Dict<Args...> const&, Unnamed2&)
{
}


TEST_CASE("cbor errors fail loudly", "[cppconfig]")
{
    using MyDict = cppdict::Dict<int, double, std::string>;

    SECTION("missing file")
    {
        REQUIRE_THROWS(load_config<MyDict>("does/not/exist.cbor"));
    }

    SECTION("empty leaf")
    {
        MyDict md;
        md["a"]["b"];
        REQUIRE_THROWS(save_config("outfile.empty_leaf.cbor", md));
    }

    SECTION("empty config round trips")
    {
        save_config("outfile.empty.cbor", MyDict {});
        REQUIRE(load_config<MyDict>("outfile.empty.cbor").isEmpty());
    }

    SECTION("empty map round trips as a map")
    {
        MyDict md;
        md["a"]["b"].data = MyDict::node_t {};
        md["c"] = 1;
        save_config("outfile.empty_map.cbor", md);
        auto const loaded = load_config<MyDict>("outfile.empty_map.cbor");
        REQUIRE(loaded["a"]["b"].isNode());
        REQUIRE(loaded["c"].to<int>() == 1);
    }

    SECTION("std::array round trips, length mismatch throws")
    {
        using ArrDict = cppdict::Dict<int, std::array<double, 3>>;
        using ArrDict2 = cppdict::Dict<int, std::array<double, 2>>;
        ArrDict ad;
        ad["arr"] = std::array<double, 3> { 1, 2, 3 };
        save_config("outfile.array.cbor", ad);
        REQUIRE(load_config<ArrDict>("outfile.array.cbor")["arr"].to<std::array<double, 3>>()
            == std::array<double, 3> { 1, 2, 3 });
        REQUIRE_THROWS(load_config<ArrDict2>("outfile.array.cbor"));
    }

    SECTION("leaf type missing from the reading Dict")
    {
        cppdict::Dict<int, std::vector<int>> md;
        md["v"] = std::vector<int> { 1 };
        REQUIRE_THROWS(from_bytes<MyDict>(to_bytes(md)));
    }

    SECTION("integer out of range")
    {
        auto const doc = json { { "format", "cppconfig" }, { "version", format_version },
            { "root", { { "x", json::array({ "i8", 300 }) } } } };
        REQUIRE_THROWS(from_bytes<cppdict::Dict<std::int8_t>>(json::to_cbor(doc)));
    }

    SECTION("not a cppconfig document")
    {
        REQUIRE_THROWS(from_bytes<MyDict>(json::to_cbor(json { { "a", 1 } })));
    }

    SECTION("ambiguous unnamed custom types")
    {
        cppdict::Dict<int, Unnamed1, Unnamed2> md;
        md["a"] = 1;
        REQUIRE_THROWS_AS(to_bytes(md), std::logic_error);
    }

    SECTION("truncated file")
    {
        std::string const file("outfile.truncated.cbor");
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
