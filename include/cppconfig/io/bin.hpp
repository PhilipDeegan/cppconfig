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
  Binary wire format (native endianness and std::size_t width, not portable across ABIs):

    file      := chunk*                          top level entries of the root map
    chunk     := map | map_entry
    map       := u8(0) size key  size(count) chunk{count}
    map_entry := u8(1) size key  leaf
    leaf      := u8(2 + index of T in Dict<Ts...>) payload(T)

    payload(fundamental T)        := raw bytes of T
    payload(Spannable T)          := size(n) raw bytes of n value_t<T>
    payload(custom serializable)  := size(count) chunk{count}
*/

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "dict.hpp"

namespace cppconfig::config_binary
{
template <typename Config>
class DictSerializer;

template <typename Config>
class DictDeSerializer;

template <typename Config>
void save_config(const std::string& file, const Config& config)
{
    DictSerializer<Config> { file }(config);
}

template <typename Config>
Config load_config(const std::string& file)
{
    return DictDeSerializer<Config> { file }();
}


template <typename Dict, typename T>
concept HasSerialize = requires(Dict& dict, T const& data) { serialize(dict, data); };

template <typename Dict, typename T>
concept HasDeserialize = requires(Dict const& dict, T& data) { deserialize(dict, data); };

template <typename Dict, typename T>
concept IsCustomSerializable = HasSerialize<Dict, T> and HasDeserialize<Dict, T>;

// owning contiguous container whose elements can be written as raw bytes, e.g. std::array
// views (std::span, ...) are excluded: a leaf must own what is read back into it
template <typename T>
concept Spannable = std::ranges::contiguous_range<T> and std::ranges::sized_range<T>
    and !std::ranges::view<T> and std::default_initializable<T>
    and std::is_trivially_copyable_v<std::ranges::range_value_t<T>>;

// Spannable whose length is only known at runtime, e.g. std::vector, std::string
template <typename T>
concept ResizableContiguous = Spannable<T> and requires(T& t, std::size_t n) { t.resize(n); };

template <typename T>
using value_t = std::ranges::range_value_t<T>;

template <typename T>
concept Fundamental = std::is_fundamental_v<T>;

template <typename Dict, typename T>
concept BinarySerializable = Fundamental<T> or Spannable<T> or IsCustomSerializable<Dict, T>;


// Width of every tag on the wire; widen if Dict ever needs more leaf types.
using tag_t = std::uint8_t;

// Leading tag written before every chunk on the wire.
enum class ChunkType : tag_t
{
    map = 0,
    map_entry = 1,
    // values >= n_base_chunk_types identify one of Config's leaf types by its index in Ts...
};

tag_t constexpr n_base_chunk_types = static_cast<tag_t>(ChunkType::map_entry) + 1;


template <typename Dict>
struct BinaryTraits;

template <typename... Ts>
struct BinaryTraits<cppdict::Dict<Ts...>>
{
    using Dict = cppdict::Dict<Ts...>;
    static constexpr std::size_t n_types = sizeof...(Ts);

    template <typename T>
    static constexpr std::size_t count_of = (std::size_t { std::same_as<T, Ts> } + ... + 0);

    static_assert(n_types + n_base_chunk_types - 1 <= std::numeric_limits<tag_t>::max(),
        "cppconfig: too many leaf types for tag_t");
    static_assert(((count_of<Ts> == 1) and ...),
        "cppconfig: Dict leaf types must be unique to have unambiguous binary tags");
    static_assert(((HasSerialize<Dict, Ts> == HasDeserialize<Dict, Ts>) and ...),
        "cppconfig: a leaf type has only one of serialize()/deserialize() - both are required");
    static_assert((BinarySerializable<Dict, Ts> and ...),
        "cppconfig: every leaf type must be fundamental, a contiguous container of trivially "
        "copyable values, or custom-serializable via serialize(Dict&, T const&) and "
        "deserialize(Dict const&, T&)");

    template <typename T>
        requires(count_of<T> == 1)
    static constexpr tag_t tag_of = []()
    {
        constexpr std::array is_T { std::same_as<T, Ts>... };
        return static_cast<tag_t>(
            std::ranges::find(is_T, true) - is_T.begin() + n_base_chunk_types);
    }();
};


template <typename T>
void _write_data(T const* data, std::size_t const size, std::ofstream& out)
{
    out.write(reinterpret_cast<char const*>(data), static_cast<std::streamsize>(size));
    if (!out)
        throw std::runtime_error("cppconfig: binary write failed");
}

template <typename T>
void _read_data(T* data, std::size_t const size, std::ifstream& in)
{
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!in or static_cast<std::size_t>(in.gcount()) != size)
        throw std::runtime_error("cppconfig: binary read failed (truncated or corrupt file)");
}


template <typename Config>
class DictSerializer
{
    using Dict = Config;
    using Traits = BinaryTraits<Dict>;
    using node_t = typename Dict::node_t;
    using empty_leaf_t = typename Dict::empty_leaf_t;

public:
    DictSerializer(std::string const& filename_) : filename { filename_ }
    {
        if (!out)
            throw std::runtime_error("cppconfig: cannot open file for writing: " + filename);
    }

    void operator()(Dict const& dict)
    {
        if (dict.isEmpty())
            return; // empty config: empty file
        if (!dict.isNode())
            throw std::runtime_error("cppconfig: root of a config must be a map");
        _write_children(std::get<node_t>(dict.data));
        out.flush();
        if (!out)
            throw std::runtime_error("cppconfig: binary write failed: " + filename);
    }

private:
    template <typename T>
    void _write(T const& value)
    {
        _write_data(&value, sizeof(T), out);
    }

    void _write_header(ChunkType const chunk, std::string const& key)
    {
        _write(chunk);
        _write(key.size());
        _write_data(key.data(), key.size(), out);
    }

    void _write_children(node_t const& map)
    {
        for (auto const& [key, child] : map)
            _write_node(key, *child);
    }

    void _write_node(std::string const& key, Dict const& node)
    {
        std::visit(
            [&]<typename El>(El const& value)
            {
                if constexpr (std::same_as<El, empty_leaf_t>)
                    throw std::runtime_error(
                        "cppconfig: cannot serialize empty leaf at key '" + key + "'");
                else if constexpr (std::same_as<El, node_t>)
                {
                    _write_header(ChunkType::map, key);
                    _write(value.size());
                    _write_children(value);
                }
                else
                {
                    _write_header(ChunkType::map_entry, key);
                    _write(Traits::template tag_of<El>);
                    _write_payload(value);
                }
            },
            node.data);
    }

    template <typename T>
    void _write_payload(T const& data)
    {
        if constexpr (Spannable<T>)
        {
            _write(std::ranges::size(data));
            _write_data(std::ranges::data(data), std::ranges::size(data) * sizeof(value_t<T>), out);
        }
        else if constexpr (Fundamental<T>)
            _write(data);
        else
        {
            static_assert(IsCustomSerializable<Dict, T>);
            Dict temp;
            serialize(temp, data);
            if (!temp.isNode())
                throw std::runtime_error(
                    "cppconfig: serialize() for a custom type must produce a map");
            auto const& map = std::get<node_t>(temp.data);
            _write(map.size());
            _write_children(map);
        }
    }


    std::string const filename;
    std::ofstream out { filename, std::ios::binary };
};


template <typename Config>
class DictDeSerializer
{
    using Dict = Config;
    using Traits = BinaryTraits<Dict>;
    using node_t = typename Dict::node_t;

public:
    DictDeSerializer(std::string const& filename_) : filename { filename_ }
    {
        if (!in)
            throw std::runtime_error("cppconfig: cannot open file for reading: " + filename);
    }

    auto operator()()
    {
        Dict dict;
        while (in.peek() != std::ifstream::traits_type::eof())
            _read_chunk(dict);
        if (in.bad())
            throw std::runtime_error("cppconfig: binary read failed: " + filename);
        return dict;
    }

private:
    template <typename T>
    T _read()
    {
        T t {};
        _read_data(&t, sizeof(T), in);
        return t;
    }

    Dict& _read_key(Dict& parent)
    {
        std::string key(_read<std::size_t>(), '\0');
        _read_data(key.data(), key.size(), in);
        auto& child = parent[key];
        if (!child.isEmpty())
            throw std::runtime_error("cppconfig: duplicate key in binary file: " + key);
        return child;
    }

    void _read_chunks(Dict& node, std::size_t const count)
    {
        node.data = node_t {}; // keep empty maps as maps
        for (std::size_t i = 0; i < count; ++i)
            _read_chunk(node);
    }

    void _read_chunk(Dict& parent)
    {
        switch (auto const type = _read<ChunkType>())
        {
            case ChunkType::map:
            {
                auto& child = _read_key(parent);
                _read_chunks(child, _read<std::size_t>());
                break;
            }
            case ChunkType::map_entry:
                _read_leaf(_read_key(parent));
                break;
            default:
                throw std::runtime_error("cppconfig: expected map or map_entry chunk, got "
                    + std::to_string(static_cast<int>(type)));
        }
    }

    void _read_leaf(Dict& node)
    {
        auto const tag = _read<tag_t>();
        if (tag < n_base_chunk_types or tag >= Traits::n_types + n_base_chunk_types)
            throw std::runtime_error("cppconfig: unknown leaf type id " + std::to_string(tag));

        [&]<typename... Ts>(cppdict::Dict<Ts...>*)
        {
            // tags are unique (BinaryTraits) and range checked, so exactly one matches
            (void)((Traits::template tag_of<Ts> == tag ? (node = _read_payload<Ts>(), true) : false)
                or ...);
        }(static_cast<Dict*>(nullptr));
    }

    template <typename T>
    T _read_payload()
    {
        if constexpr (Spannable<T>)
        {
            T s {};
            auto const size = _read<std::size_t>();
            if constexpr (ResizableContiguous<T>)
                s.resize(size);
            else if (size != std::ranges::size(s))
                throw std::runtime_error(
                    "cppconfig: fixed size container length mismatch, file has "
                    + std::to_string(size) + " expected " + std::to_string(std::ranges::size(s)));
            _read_data(std::ranges::data(s), size * sizeof(value_t<T>), in);
            return s;
        }
        else if constexpr (Fundamental<T>)
            return _read<T>();
        else
        {
            static_assert(IsCustomSerializable<Dict, T>);
            Dict loader;
            _read_chunks(loader, _read<std::size_t>());
            T t;
            deserialize(loader, t);
            return t;
        }
    }


    std::string const filename;
    std::ifstream in { filename, std::ios::binary };
};


} // namespace cppconfig::config_binary
