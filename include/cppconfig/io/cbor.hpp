/*------------------------------------------------------------------------------
--  This file is a part of the C++ Config library
--  Copyright (C) 2022, Plasma Physics Laboratory - CNRS
--
--  This program is free software; you can redistribute it and/or modify
--  it under the terms of the GNU General Public License as published by
--  the Free Software Foundation; either version 2 of the License, or
--  (at your option) any later version.
--
--  This program is distributed in the hope that it will be useful,
--  but WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--  GNU General Public License for more details.
--
--  You should have received a copy of the GNU General Public License
--  along with this program; if not, write to the Free Software
--  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
-------------------------------------------------------------------------------*/
/*--                  Author : Alexis Jeandet
--                     Mail : alexis.jeandet@lpp.polytechnique.fr
----------------------------------------------------------------------------*/
#pragma once

/*
  CBOR (RFC 8949) file layout, portable across endianness and ABIs:

    file  := { "format": "cppconfig", "version": 1, "root": map | null }
    node  := map | leaf
    map   := { key: node, ... }
    leaf  := [ type_name, value ]

    value(bool)                     := bool
    value(integral)                 := integer         range checked on load
    value(floating point)           := float
    value(range of char)            := text string     e.g. std::string
    value(range of arithmetic)      := [ value, ... ]  e.g. std::vector<double>, std::array<int, 3>
    value(custom serializable)      := map             the Dict produced by serialize(Dict&, T
  const&)

  type_name is derived from the type itself, not from its position in Dict<Ts...>, so reordering
  or adding leaf types does not invalidate existing files:
    "bool", "i8".."i64", "u8".."u64", "f32", "f64", "str", "str[N]", "vec<E>", "arr<E,N>"
  Custom types are named "obj" unless an ADL-findable
    cppconfig_type_name(std::type_identity<T>) -> string
  is provided. Names must be unique within a Dict, so a Dict holding more than one custom type
  must name them. Prefer fixed width integers (std::int64_t, ...) as leaf types: `long` is "i64"
  on Linux but "i32" on Windows.
*/

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "dict.hpp"

namespace cppconfig::config_cbor
{
using json = nlohmann::json;

inline constexpr int format_version = 1;


template <typename Config>
std::vector<std::uint8_t> to_bytes(Config const& config);

template <typename Config>
Config from_bytes(std::vector<std::uint8_t> const& bytes);

template <typename Config>
void save_config(std::filesystem::path const& file, Config const& config);

template <typename Config>
Config load_config(std::filesystem::path const& file);


template <typename Dict, typename T>
concept HasSerialize = requires(Dict& dict, T const& data) { serialize(dict, data); };

template <typename Dict, typename T>
concept HasDeserialize = requires(Dict const& dict, T& data) { deserialize(dict, data); };

template <typename Dict, typename T>
concept IsCustomSerializable = HasSerialize<Dict, T> and HasDeserialize<Dict, T>;

} // namespace cppconfig::config_cbor


namespace cppconfig::config_cbor::detail
{
template <typename T>
concept HasTypeName = requires {
    { cppconfig_type_name(std::type_identity<T> {}) } -> std::convertible_to<std::string>;
};

template <typename T>
concept CharLike = std::same_as<T, char> or std::same_as<T, wchar_t> or std::same_as<T, char8_t>
    or std::same_as<T, char16_t> or std::same_as<T, char32_t>;

// arithmetic types with a platform independent meaning
template <typename T>
concept PortableArithmetic
    = std::same_as<T, bool> or (std::integral<T> and !CharLike<T> and sizeof(T) <= 8)
    or (std::floating_point<T> and std::numeric_limits<T>::is_iec559
        and (sizeof(T) == 4 or sizeof(T) == 8));

// owning sized range, e.g. std::vector, std::array, std::string
template <typename T>
concept Sequence = std::ranges::forward_range<T> and std::ranges::sized_range<T>
    and !std::ranges::view<T> and std::default_initializable<T>;

template <typename T>
concept Resizable = Sequence<T> and requires(T& t, std::size_t n) { t.resize(n); };

template <typename T>
concept Text = Sequence<T> and std::same_as<std::ranges::range_value_t<T>, char>;

template <typename T>
concept NumericSequence
    = Sequence<T> and !Text<T> and PortableArithmetic<std::ranges::range_value_t<T>>;
} // namespace cppconfig::config_cbor::detail


namespace cppconfig::config_cbor
{
template <typename Dict, typename T>
concept CborSerializable = IsCustomSerializable<Dict, T> or detail::PortableArithmetic<T>
    or detail::Text<T> or detail::NumericSequence<T>;
} // namespace cppconfig::config_cbor


namespace cppconfig::config_cbor::detail
{
[[noreturn]] inline void fail(std::string const& what, std::string const& path);

template <typename Dict, typename T>
std::string type_name();

template <typename T>
std::string custom_type_name();

template <typename Dict, typename T>
std::string sequence_type_name();

template <typename T>
std::string extent_of();

// everything that needs the Ts... pack of a Dict
template <typename Dict>
struct LeafTypes;

template <typename... Ts>
struct LeafTypes<cppdict::Dict<Ts...>>
{
    using Dict = cppdict::Dict<Ts...>;

    static_assert(((HasSerialize<Dict, Ts> == HasDeserialize<Dict, Ts>) and ...),
        "cppconfig: a leaf type has only one of serialize()/deserialize() - both are required");
    static_assert((CborSerializable<Dict, Ts> and ...),
        "cppconfig: every leaf type must be bool, a fixed size integer, an IEEE float/double, "
        "a range of char, a range of those arithmetic types, or custom-serializable via "
        "serialize(Dict&, T const&) and deserialize(Dict const&, T&)");

    static void check_unique_names();

    // calls f.template operator()<T>() for the leaf type T named `name`, false if none matches
    template <typename F>
    static bool dispatch(std::string const& name, F&& f);
};


template <typename Dict>
struct Codec
{
    using node_t = typename Dict::node_t;
    using empty_leaf_t = typename Dict::empty_leaf_t;

    static json encode_node(Dict const& node, std::string const& path);
    static void decode_node(json const& j, Dict& node, std::string const& path);

private:
    static json encode_alternative(empty_leaf_t const&, std::string const& path);
    static json encode_alternative(node_t const& map, std::string const& path);
    template <typename T>
    static json encode_alternative(T const& leaf, std::string const& path);

    template <typename T>
    static json encode_value(T const& value, std::string const& path);
    template <typename T>
    static json encode_custom(T const& value, std::string const& path);
    template <typename T>
    static json encode_sequence(T const& value, std::string const& path);

    static void decode_leaf(
        std::string const& name, json const& value, Dict& node, std::string const& path);

    template <typename T>
    static T decode_value(json const& j, std::string const& path);
    template <typename T>
    static T decode_custom(json const& j, std::string const& path);
    template <typename T>
    static T decode_text(json const& j, std::string const& path);
    template <typename T>
    static T decode_sequence(json const& j, std::string const& path);
    template <typename T>
    static T decode_integral(json const& j, std::string const& path);
    template <typename T>
    static T decode_floating(json const& j, std::string const& path);

    template <typename T>
    static T sized(std::size_t n, std::string const& path);
};


// ---------------------------------------------------------------- helpers

[[noreturn]] inline void fail(std::string const& what, std::string const& path)
{
    throw std::runtime_error("cppconfig: " + what + " at '" + path + "'");
}

template <typename T>
std::string extent_of()
{
    return std::to_string(std::ranges::size(T {}));
}

template <typename T>
std::string custom_type_name()
{
    if constexpr (HasTypeName<T>)
        return std::string { cppconfig_type_name(std::type_identity<T> {}) };
    return "obj";
}

template <typename Dict, typename T>
std::string sequence_type_name()
{
    auto const element = type_name<Dict, std::ranges::range_value_t<T>>();
    if constexpr (Resizable<T>)
        return "vec<" + element + ">";
    return "arr<" + element + "," + extent_of<T>() + ">";
}

template <typename Dict, typename T>
std::string type_name()
{
    if constexpr (IsCustomSerializable<Dict, T>)
        return custom_type_name<T>();
    else if constexpr (std::same_as<T, bool>)
        return "bool";
    else if constexpr (std::integral<T>)
        return (std::is_signed_v<T> ? "i" : "u") + std::to_string(8 * sizeof(T));
    else if constexpr (std::floating_point<T>)
        return "f" + std::to_string(8 * sizeof(T));
    else if constexpr (Text<T> and Resizable<T>)
        return "str";
    else if constexpr (Text<T>)
        return "str[" + extent_of<T>() + "]";
    else
        return sequence_type_name<Dict, T>();
}


template <typename... Ts>
void LeafTypes<cppdict::Dict<Ts...>>::check_unique_names()
{
    static auto const duplicate = []() -> std::string
    {
        std::vector<std::string> names { type_name<Dict, Ts>()... };
        std::ranges::sort(names);
        auto const it = std::ranges::adjacent_find(names);
        return it == names.end() ? "" : *it;
    }();

    if (!duplicate.empty())
        throw std::logic_error("cppconfig: duplicate leaf type name '" + duplicate
            + "', provide cppconfig_type_name(std::type_identity<T>) for custom types or use "
              "distinct fixed width types");
}

template <typename... Ts>
template <typename F>
bool LeafTypes<cppdict::Dict<Ts...>>::dispatch(std::string const& name, F&& f)
{
    return ((name == type_name<Dict, Ts>() ? (f.template operator()<Ts>(), true) : false) or ...);
}


// ---------------------------------------------------------------- encode

template <typename Dict>
json Codec<Dict>::encode_node(Dict const& node, std::string const& path)
{
    return std::visit(
        [&](auto const& value) { return encode_alternative(value, path); }, node.data);
}

template <typename Dict>
json Codec<Dict>::encode_alternative(empty_leaf_t const&, std::string const& path)
{
    fail("cannot serialize empty leaf", path);
}

template <typename Dict>
json Codec<Dict>::encode_alternative(node_t const& map, std::string const& path)
{
    json obj = json::object();
    for (auto const& [key, child] : map)
        obj[key] = encode_node(*child, path + "/" + key);
    return obj;
}

template <typename Dict>
template <typename T>
json Codec<Dict>::encode_alternative(T const& leaf, std::string const& path)
{
    return json::array({ type_name<Dict, T>(), encode_value(leaf, path) });
}

template <typename Dict>
template <typename T>
json Codec<Dict>::encode_value(T const& value, std::string const& path)
{
    if constexpr (IsCustomSerializable<Dict, T>)
        return encode_custom(value, path);
    else if constexpr (Text<T>)
        return std::string(std::ranges::begin(value), std::ranges::end(value));
    else if constexpr (NumericSequence<T>)
        return encode_sequence(value, path);
    else
        return json(value);
}

template <typename Dict>
template <typename T>
json Codec<Dict>::encode_custom(T const& value, std::string const& path)
{
    Dict temp;
    serialize(temp, value);
    if (temp.isEmpty())
        return json::object();
    if (!temp.isNode())
        fail("serialize() for a custom type must produce a map", path);
    return encode_node(temp, path);
}

template <typename Dict>
template <typename T>
json Codec<Dict>::encode_sequence(T const& value, std::string const& path)
{
    json arr = json::array();
    for (auto const& e : value)
        arr.push_back(encode_value<std::ranges::range_value_t<T>>(e, path));
    return arr;
}


// ---------------------------------------------------------------- decode

template <typename Dict>
void Codec<Dict>::decode_node(json const& j, Dict& node, std::string const& path)
{
    if (j.is_array() and j.size() == 2 and j[0].is_string())
        return decode_leaf(j[0].get_ref<std::string const&>(), j[1], node, path);

    if (!j.is_object())
        fail("expected a map or a [type, value] leaf", path);

    node.data = node_t {}; // keep empty maps as maps
    for (auto const& [key, value] : j.items())
        decode_node(value, node[key], path + "/" + key);
}

template <typename Dict>
void Codec<Dict>::decode_leaf(
    std::string const& name, json const& value, Dict& node, std::string const& path)
{
    auto const decode = [&]<typename T>() { node = decode_value<T>(value, path); };
    if (!LeafTypes<Dict>::dispatch(name, decode))
        fail("unknown leaf type '" + name + "'", path);
}

template <typename Dict>
template <typename T>
T Codec<Dict>::decode_value(json const& j, std::string const& path)
{
    if constexpr (IsCustomSerializable<Dict, T>)
        return decode_custom<T>(j, path);
    else if constexpr (Text<T>)
        return decode_text<T>(j, path);
    else if constexpr (NumericSequence<T>)
        return decode_sequence<T>(j, path);
    else if constexpr (std::same_as<T, bool>)
        return j.is_boolean() ? j.get<bool>() : (fail("expected a bool", path), false);
    else if constexpr (std::integral<T>)
        return decode_integral<T>(j, path);
    else
        return decode_floating<T>(j, path);
}

template <typename Dict>
template <typename T>
T Codec<Dict>::decode_custom(json const& j, std::string const& path)
{
    if (!j.is_object())
        fail("expected a map for custom type '" + type_name<Dict, T>() + "'", path);
    Dict loader;
    decode_node(j, loader, path);
    T t;
    deserialize(loader, t);
    return t;
}

template <typename Dict>
template <typename T>
T Codec<Dict>::decode_text(json const& j, std::string const& path)
{
    if (!j.is_string())
        fail("expected a string", path);
    auto const& s = j.get_ref<std::string const&>();
    auto t = sized<T>(s.size(), path);
    std::ranges::copy(s, std::ranges::begin(t));
    return t;
}

template <typename Dict>
template <typename T>
T Codec<Dict>::decode_sequence(json const& j, std::string const& path)
{
    if (!j.is_array())
        fail("expected an array", path);
    auto t = sized<T>(j.size(), path);
    auto it = std::ranges::begin(t);
    for (auto const& e : j)
        *it++ = decode_value<std::ranges::range_value_t<T>>(e, path);
    return t;
}

template <typename Dict>
template <typename T>
T Codec<Dict>::decode_integral(json const& j, std::string const& path)
{
    auto const narrow = [&](auto const v)
    {
        if (!std::in_range<T>(v))
            fail(
                "integer " + std::to_string(v) + " out of range for " + type_name<Dict, T>(), path);
        return static_cast<T>(v);
    };

    if (j.is_number_unsigned())
        return narrow(j.get<std::uint64_t>());
    if (j.is_number_integer())
        return narrow(j.get<std::int64_t>());
    fail("expected an integer", path);
}

template <typename Dict>
template <typename T>
T Codec<Dict>::decode_floating(json const& j, std::string const& path)
{
    if (!j.is_number_float())
        fail("expected a float", path);
    auto const v = j.get<double>();
    if (std::isfinite(v) and std::abs(v) > std::numeric_limits<T>::max())
        fail("float " + std::to_string(v) + " out of range for " + type_name<Dict, T>(), path);
    return static_cast<T>(v);
}

template <typename Dict>
template <typename T>
T Codec<Dict>::sized(std::size_t const n, std::string const& path)
{
    T t {};
    if constexpr (Resizable<T>)
        t.resize(n);
    else if (std::ranges::size(t) != n)
        fail("fixed size container length mismatch, file has " + std::to_string(n) + " expected "
                + std::to_string(std::ranges::size(t)),
            path);
    return t;
}


} // namespace cppconfig::config_cbor::detail


namespace cppconfig::config_cbor
{


// ---------------------------------------------------------------- public API

template <typename Config>
std::vector<std::uint8_t> to_bytes(Config const& config)
{
    detail::LeafTypes<Config>::check_unique_names();
    if (!config.isNode() and !config.isEmpty())
        throw std::runtime_error("cppconfig: root of a config must be a map");

    auto const root = config.isNode() ? detail::Codec<Config>::encode_node(config, "") : json {};
    return json::to_cbor(
        json { { "format", "cppconfig" }, { "version", format_version }, { "root", root } });
}

template <typename Config>
Config from_bytes(std::vector<std::uint8_t> const& bytes)
{
    detail::LeafTypes<Config>::check_unique_names();

    auto const doc = json::from_cbor(bytes);
    if (!doc.is_object() or doc.value("format", "") != "cppconfig")
        throw std::runtime_error("cppconfig: not a cppconfig CBOR document");
    if (!doc.contains("version") or doc["version"] != format_version)
        throw std::runtime_error("cppconfig: unsupported cppconfig CBOR format version");

    auto const& root = doc.value("root", json {});
    if (!root.is_object() and !root.is_null())
        throw std::runtime_error("cppconfig: root of a config must be a map");

    Config config;
    if (root.is_object())
        detail::Codec<Config>::decode_node(root, config, "");
    return config;
}

template <typename Config>
void save_config(std::filesystem::path const& file, Config const& config)
{
    auto const bytes = to_bytes(config);
    std::ofstream out { file, std::ios::binary };
    if (!out)
        throw std::runtime_error("cppconfig: cannot open file for writing: " + file.string());
    out.write(
        reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out.flush())
        throw std::runtime_error("cppconfig: write failed: " + file.string());
}

template <typename Config>
Config load_config(std::filesystem::path const& file)
{
    std::ifstream in { file, std::ios::binary };
    if (!in)
        throw std::runtime_error("cppconfig: cannot open file for reading: " + file.string());
    std::vector<std::uint8_t> const bytes { std::istreambuf_iterator<char> { in }, {} };
    if (in.bad())
        throw std::runtime_error("cppconfig: read failed: " + file.string());
    return from_bytes<Config>(bytes);
}

} // namespace cppconfig::config_cbor
