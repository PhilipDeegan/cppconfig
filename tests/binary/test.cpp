
// #include <catch2/catch.hpp>
// #include <catch2/catch_reporter_tap.hpp>
// #include <catch2/catch_reporter_teamcity.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cppconfig/io/bin.hpp"


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

        // std::cout << __FILE__ << " " << __LINE__ << std::endl;
        // cppconfig::config_binary::print_dict(md);
        cppconfig::config_binary::save_config(file, md);
    }

    auto const md = cppconfig::config_binary::load_config<MyDict>(file);
    // std::cout << __FILE__ << " " << __LINE__ << std::endl;
    // cppconfig::config_binary::print_dict(md);

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

    bool operator==(CustomSerializable const& cs) { return a == cs.a and b == cs.b; }
    bool operator!=(CustomSerializable const& cs) { return !(*this == cs); }
};


template <typename... Args>
void serialize(cppdict::Dict<Args...>& dict, CustomSerializable const& cm)
{
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    dict["cm.a"] = cm.a;
    dict["cm.b"] = cm.b;
}

template <typename... Args>
void deserialize(cppdict::Dict<Args...> const& dict, CustomSerializable& cm)
{
    std::cout << __FILE__ << " " << __LINE__ << std::endl;
    cm.a = dict["cm.a"].template to<int>();
    cm.b = dict["cm.b"].template to<double>();
}


TEST_CASE("can read/write custom serializable", "[cppconfig]")
{
    using MyDict = cppdict::Dict<int, double, std::string, CustomSerializable>;
    static_assert(cppconfig::config_binary::is_custom_serializable_v<MyDict, CustomSerializable>);

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
            static_assert(cppconfig::config_binary::has_custom_serialize(md, cs));

            std::cout << __FILE__ << " " << __LINE__ << std::endl;
            // cppconfig::config_binary::print_dict(md);
            cppconfig::config_binary::save_config(file, md);
        }

        std::cout << __FILE__ << " " << __LINE__ << std::endl;
        auto const md = cppconfig::config_binary::load_config<MyDict>(file);
        // cppconfig::config_binary::print_dict(md);

        SECTION("A dict")
        {
            REQUIRE(md["key"].to<std::string>() == "Value");
            REQUIRE(md["custom"]["data"]["cm.a"].to<int>() == cs.a);
            REQUIRE(md["custom"]["data"]["cm.b"].to<double>() == cs.b);
            REQUIRE(md["custom"]["other"].to<double>() == 3.3);
            REQUIRE(md["key2"].to<int>() == 2);
        }
    }

    // {
    //     std::string const file("outfile.1.dat");

    //     {
    //         MyDict md;
    //         md["key"] = std::string { "Value" };
    //         md["custom"]["data"] = cs;
    //         md["custom"]["other"] = 3.3;
    //         md["key2"] = 2;
    //         static_assert(cppconfig::config_binary::has_custom_serialize(md, cs));

    //         std::cout << __FILE__ << " " << __LINE__ << std::endl;
    //         // cppconfig::config_binary::print_dict(md);
    //         cppconfig::config_binary::save_config(file, md);
    //     }

    //     std::cout << __FILE__ << " " << __LINE__ << std::endl;
    //     auto const md = cppconfig::config_binary::load_config<MyDict>(file);
    //     cppconfig::config_binary::print_dict(md);

    //     SECTION("A dict")
    //     {
    //         REQUIRE(md["key"].to<std::string>() == "Value");
    //         // REQUIRE(md["custom"]["other"].to<double>() == 3.3);
    //         // REQUIRE(md["custom"]["data"].to<CustomSerializable>() == cs);
    //         // REQUIRE(md["key2"].to<int>() == 2);
    //     }
    // }
}

// struct UnSerializable
// {
//     int a = 1;
//     double b = 3;
// };

// TEST_CASE("can read/write unserializable", "[cppconfig]")
// {
//     using MyDict = cppdict::Dict<int, double, std::string, UnSerializable>;

//     std::string const file("outfile.dat");

//     {
//         MyDict md;
//         md["test"]["super"] = 2;
//         md["PI"] = 3.14;
//         md["Key"] = std::string { "Value" };
//         md["key2"] = 2;
//         md["custom"] = UnSerializable {};


//         cppconfig::config_binary::save_config(file, md);
//     }

//     auto const md = cppconfig::config_binary::load_config<MyDict>(file);

//     // SECTION("A dict")
//     // {
//     //     REQUIRE(md["custom"].to<CustomSerializable>() != CustomSerializable{});
//     // }
// }
