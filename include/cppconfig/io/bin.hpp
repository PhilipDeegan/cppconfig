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
#include <dict.hpp>
#include <stdexcept>
#include <variant>

#ifndef CPPDICT_DICT_EXP_SERIALZIER_HPP
#define CPPDICT_DICT_EXP_SERIALZIER_HPP

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>


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

template <typename... Args>
auto constexpr dict_types_as_tuple(cppdict::Dict<Args...> const&)
{
    return std::tuple<Args...> {};
}


template <typename T>
void _read_data(T data, std::size_t const size, std::ifstream& in)
{
    in.read(reinterpret_cast<char*>(data), size);
}

template <typename T>
void _write_data(T const data, std::size_t const size, std::ofstream& out)
{
    out.write(reinterpret_cast<char const* const>(data), size);
    // std::cout.write(reinterpret_cast<char const* const>(data), size);
}

template <typename...>
using tryToInstanciate = void;

template <typename T, typename data = void, typename size = void>
struct is_spannable : std::false_type
{
};

template <typename T>
struct is_spannable<T, tryToInstanciate<decltype(std::declval<T>().data())>,
    tryToInstanciate<decltype(std::declval<T>().size())>> : std::true_type
{
};

template <typename T>
bool constexpr static is_spannable_v = is_spannable<T>::value;


template <typename Dict, typename T, typename serializable = void, typename unserializable = void>
struct is_custom_serializable : std::false_type
{
};

template <typename Dict, typename T>
auto constexpr static has_custom_serialize(Dict& dict, T const& data)
    -> decltype(serialize(dict, data), bool())
{
    return true;
}
template <typename... Args>
auto constexpr static has_custom_serialize(Args&&...)
{
    return false;
}
template <typename Dict, typename T>
auto constexpr static has_custom_deserialize(Dict const& dict, T& data)
    -> decltype(deserialize(dict, data), bool())
{
    return true;
}
template <typename... Args>
auto constexpr static has_custom_deserialize(Args&&...)
{
    return false;
}


template <typename Dict, typename T>
struct is_custom_serializable<Dict, T,
    tryToInstanciate<decltype(serialize(std::declval<Dict&>(), std::declval<T const&>()))>,
    tryToInstanciate<decltype(deserialize(std::declval<Dict const&>(), std::declval<T&>()))>>
        : std::true_type
{
};


template <typename Dict, typename T>
bool constexpr static is_custom_serializable_v = is_custom_serializable<Dict, T>::value;


template <typename Dict, typename T>
auto constexpr static _custom_serialize(Dict& dict, T const& data)
    -> decltype(serialize(dict, data), bool())
{
    serialize(dict, data);
    return true;
}

template <typename... Args>
auto constexpr static _custom_serialize(Args&&...)
{
    return false;
}


template <typename Dict, typename T>
auto constexpr static _custom_deserialize(Dict const& dict, T& data)
    -> decltype(deserialize(dict, data), bool())
{
    deserialize(dict, data);
    return true;
}

template <typename... Args>
auto constexpr static _custom_deserialize(Args&&...)
{
    return false;
}


// template <typename Config>
// void print_dict(Config const& dict, std::string const& prefix = "");

template <typename Config, typename T>
void _print_dict_val(std::string const& prefix, T const& val);


template <typename... Ts>
struct varient_visitor_overloads : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts>
varient_visitor_overloads(Ts&&...) -> varient_visitor_overloads<std::decay_t<Ts>...>;


template <typename Config, typename... Ts>
void _print_dict(std::string const& prefix, std::variant<Ts...> const& val)
{
    auto const overloads = varient_visitor_overloads { [&](const typename Config::empty_leaf_t&) {},
        [&](auto&& value)
        {
            using El = std::decay_t<decltype(value)>;
            static_assert(!std::is_same_v<El, typename Config::empty_leaf_t>);
            if constexpr (Config::template is_value_v<El>)
                _print_dict_val<Config>(prefix, value);
            else
                for (const auto& [k, node] : value)
                    _print_dict<Config>(prefix + k, node->data);
        } };

    std::visit(overloads, val);
}


template <typename Config, typename... Ts>
void _print_dict(std::string const& prefix, cppdict::Dict<Ts...> const& dict)
{
    using node_t = typename Config::node_t;

    auto const value_visitor = [&](const std::string& key, const auto& v)
    {
        using El = std::decay_t<decltype(v)>;
        if constexpr (Config::template is_value_v<El>)
            _print_dict_val<Config>(prefix + key, v);
        else
            _print_dict<Config>(prefix, v);
    };

    std::function<void(std::string const&, node_t const&)> const node_visitor
        = [&](std::string const& key, node_t const& v)
    {
        for (const auto& [k, node] : v)
        {
            if (node->isNode())
                node_visitor(k, std::get<node_t>(node->data));
            else
                _print_dict<Config>(prefix + key + "/" + k, *node);
        }
    };

    if (dict.isNode())
        dict.visit(
            cppdict::visit_all_nodes, node_visitor,
            [&](const std::string&, const typename Config::empty_leaf_t&) { }, value_visitor);
    else if (dict.isValue())
        value_visitor("", dict.data);
    else
    {
        // throw std::runtime_error(prefix + " print_dict else?");
    }
}


template <typename Config, typename T>
void _print_dict_val(std::string const& prefix, T const& val)
{
    if constexpr (is_spannable_v<T>)
    {
        if (!val.size())
            return;
        std::cout << prefix << " = " << val[0];
        for (std::size_t i = 1; i < val.size(); ++i)
            std::cout << "," << val[i];
        std::cout << std::endl;
    }
    else if constexpr (std::is_fundamental<T>::value)
    {
        std::cout << prefix << " = " << val << std::endl;
    }
    else if constexpr (is_custom_serializable_v<Config, T>)
    {
        Config temp;
        static_assert(has_custom_serialize(temp, val));
        bool const done = _custom_serialize(temp, val);
        if (!done)
            throw std::runtime_error("NOT DONE!");
        _print_dict<Config>(prefix + "/", temp);
    }
    else
    {
        _print_dict<Config>(prefix, val);
    }
}


template <typename Config>
void print_dict(Config const& dict)
{
    _print_dict<Config>("/", dict);
}


template <typename Config>
class DictSerializer
{
    using Dict = Config;
    using data_t = typename Dict::data_t;
    using node_t = typename Dict::node_t;
    using Tuple = std::decay_t<decltype(dict_types_as_tuple(std::declval<Config>()))>;
    std::size_t constexpr static n_types = std::tuple_size_v<Tuple>;
    std::size_t constexpr static base = 2; // 0 = map, 1 = map_key_string
    using NodeVisitor = std::function<void(std::string const&, node_t const&)>;

public:
    DictSerializer(std::string const& filename_) : filename { filename_ } { }


    void operator()(Dict const& dict) { _serialize(dict); }


private:
    void _serialize(Config const& dict, std::string const& in = "")
    {
        // std::cout << __FILE__ << " " << __LINE__ << " " << in << std::endl;

        auto const value_visitor = [&](const std::string& key, const auto& v)
        {
            // std::cout << __FILE__ << " " << __LINE__ << " " << key << std::endl;


            using El = std::decay_t<decltype(v)>;
            static_assert(!std::is_same_v<El, node_t>);

            if constexpr (std::is_same_v<El, data_t>)
            {

                // std::cout << __FILE__ << " " << __LINE__ << " " << key << std::endl;

                std::size_t const type = 1;
                _write_data(&type, sizeof(type), out);
                std::size_t const size = key.size();
                _write_data(&size, sizeof(size), out);
                _write_data(key.data(), size, out);

                _serialize(v);
            }
            else if constexpr (Dict::template is_value_v<El>)
            {
                std::size_t const type = 1;
                _write_data(&type, sizeof(type), out);
                std::size_t const size = key.size();
                _write_data(&size, sizeof(size), out);
                _write_data(key.data(), size, out);

                // std::cout << __FILE__ << " " << __LINE__ << " " << key << std::endl;
                write_type_id(v, std::make_integer_sequence<std::size_t, n_types> {});
                _serialize_data(v);
            }
            else
                for (const auto& [k, node] : v)
                    _serialize(node, k);
        };

        if (dict.isNode())
            dict.visit(
                cppdict::visit_all_nodes, node_visitor,
                [&](const std::string&, const typename Dict::empty_leaf_t&) { }, value_visitor);
        else if (dict.isValue())
        {
            // std::cout << __FILE__ << " " << __LINE__ << " " << in << std::endl;
            if (in.empty())
                throw std::runtime_error("no in!");
            value_visitor(in, dict.data);
        }
        else
            throw std::runtime_error("else?");
    }


    template <typename... Ts>
    void _serialize(std::variant<Ts...> const& val)
    {
        auto const overloads
            = varient_visitor_overloads { [&](const typename Config::empty_leaf_t&) {},
                  [&](auto&& value)
                  {
                      using El = std::decay_t<decltype(value)>;
                      static_assert(!std::is_same_v<El, typename Config::empty_leaf_t>);
                      if constexpr (Config::template is_value_v<El>)
                      {

                          // std::cout << __FILE__ << " " << __LINE__ << " " << std::endl;
                          write_type_id(value, std::make_integer_sequence<std::size_t, n_types> {});
                          _serialize_data(value);
                      }
                      else
                      {
                          throw std::runtime_error("what");
                          for (const auto& [k, node] : value)
                              _serialize(*node, k);
                      }
                  } };

        std::visit(overloads, val);
    }


    NodeVisitor const node_visitor = [&](std::string const& key, node_t const& v)
    {
        // std::cout << __FILE__ << " " << __LINE__ << " " << key <<std::endl;


        // std::cout << __FILE__ << " " << __LINE__ << " keys " << keys <<std::endl;
        // for (const auto& [_, __] : v)
        //     // if (is_node_or_serializable(*node))
        //         ++keys;

        {
            std::size_t const type = 0;
            _write_data(&type, sizeof(type), out);
            std::size_t const size = key.size();
            _write_data(&size, sizeof(size), out);
            std::size_t const keys = v.size();
            _write_data(&keys, sizeof(keys), out);
            _write_data(key.data(), size, out);
        }

        for (const auto& [k, node] : v)
        {

            if (node->isNode())
                node_visitor(k, std::get<node_t>(node->data));
            else if (node->isValue())
            {

                _serialize(*node, k);
            }
            else
                throw std::runtime_error("node_visitor else?");
        }
    };


    // template <std::size_t I>
    // bool _holds_serializable(data_t const& data) const
    // {
    //     using El = std::tuple_element_t<I, Tuple>;

    //     if constexpr (Dict::template is_value_v<El>)
    //         return std::holds_alternative<El>(data);
    //     return false;
    // }

    // template <std::size_t... Is>
    // bool holds_serializable(data_t const& data, std::integer_sequence<std::size_t, Is...>) const
    // {
    //     int any = 0;
    //     ((any += _holds_serializable<Is>(data)), ...);
    //     return any > 0;
    // }

    // bool is_node_or_serializable(Dict const& dict) const
    // {
    //     return dict.isNode()
    //         || holds_serializable(dict.data, std::make_integer_sequence<std::size_t, n_types>
    //         {});
    // };


    template <typename T>
    void _serialize_data(T const& data)
    {
        // std::cout << __FILE__ << " " << __LINE__ << std::endl;
        if constexpr (is_spannable_v<T>)
        {
            // std::cout << __FILE__ << " " << __LINE__ << " " << data[0] << std::endl;
            std::size_t const size = data.size();
            _write_data(&size, sizeof(size), out);
            _write_data(data.data(), size * sizeof(typename T::value_type), out);
        }
        else if constexpr (std::is_fundamental<T>::value)
        {
            // std::cout << __FILE__ << " " << __LINE__ << " " << data << std::endl;
            std::size_t const size = sizeof(T);
            _write_data(&data, size, out);
        }
        else if constexpr (is_custom_serializable_v<Config, T>)
        {
            // std::cout << __FILE__ << " " << __LINE__ << std::endl;
            Dict temp;
            bool const success = _custom_serialize(temp, data);
            if (!success)
                throw std::runtime_error("serialize failed!");
            (*this)(temp);
        }
        else
        {
            _serialize(data);
            // throw std::runtime_error("_serialize_data failed");
        }
    }


    template <typename T, std::size_t I>
    void write_type_id(T const&, bool& b)
    {
        if (b)
            return;
        if constexpr (std::is_same_v<T, std::tuple_element_t<I, Tuple>>)
        {
            std::size_t const type = I + base;
            _write_data(&type, sizeof(type), out);
            b = 1;
            // std::cout << __FILE__ << " " << __LINE__ << " " << type << std::endl;
        }
    }

    template <typename T, std::size_t... Is>
    void write_type_id(T const& t, std::integer_sequence<std::size_t, Is...>)
    {
        std::cout << typeid(T).name() << std::endl;
        bool b = 0;
        (write_type_id<T, Is>(t, b), ...);
        if (b == 0)
            throw std::runtime_error("serialize write_type_id failed!");
    }

    std::string const filename;
    std::ofstream out { filename, std::ios::binary };
};


template <typename Config>
class DictDeSerializer
{
    using Dict = Config;
    using data_t = typename Dict::data_t;
    using Tuple = std::decay_t<decltype(dict_types_as_tuple(std::declval<Config>()))>;
    std::size_t constexpr static n_types = std::tuple_size_v<Tuple>;
    std::size_t constexpr static base = 2; // 0 = map, 1 = map_key_string

public:
    DictDeSerializer(std::string const& filename_) : filename { filename_ } { }

    template <bool silence_unserializable = false>
    auto operator()()
    {
        Dict dict;

        std::function<void(Dict&)> read_chunk = [&](auto& node)
        {
            std::cout << __FILE__ << " " << __LINE__ << std::endl;
            print_dict<Config>(node);

            std::size_t type = 0;

            in.read(reinterpret_cast<char*>(&type), sizeof(std::size_t));
            if (type > n_types + 2)
                throw std::runtime_error("whaaa " + std::to_string(type));
            // std::cout << __FILE__ << " " << __LINE__ << " " << type << std::endl;

            if (type == 0)
            {
                std::size_t size = 0;
                in.read(reinterpret_cast<char*>(&size), sizeof(std::size_t));

                std::string s;
                s.resize(size);

                std::size_t keys = 0;
                in.read(reinterpret_cast<char*>(&keys), sizeof(std::size_t));
                _read_data(s.data(), size, in);


                // std::cout << __FILE__ << " " << __LINE__ << " " << s << " " << keys << std::endl;
                // std::cout << __FILE__ << " " << __LINE__ << " " << keys << std::endl;
                auto& child = node[s];

                for (std::size_t i = 0; i < keys; ++i)
                    read_chunk(child);
            }
            else if (type == 1)
            {
                std::size_t size = 0;
                in.read(reinterpret_cast<char*>(&size), sizeof(std::size_t));

                std::string s;
                s.resize(size);
                _read_data(s.data(), size, in);

                // std::cout << __FILE__ << " " << __LINE__ << " " << s << std::endl;

                read_chunk(node[s]);
            }
            else
            {
                read(node, type, std::make_integer_sequence<std::size_t, n_types> {});
            }
        };

        while (!in.eof())
        {
            read_chunk(dict);
            in.peek();
        }

        return dict;
    }

private:
    template <typename T>
    void _deserialize_data(Dict& node)
    {
        if (!node.isEmpty())
            throw std::runtime_error("NODE NOT EMPTY");

        if constexpr (is_spannable_v<T>)
        {
            std::size_t size = 0;

            in.read(reinterpret_cast<char*>(&size), sizeof(std::size_t));

            T s;
            s.resize(size);
            _read_data(s.data(), size * sizeof(typename T::value_type), in);
            std::cout << __FILE__ << " " << __LINE__ << " " << s[0] << std::endl;

            node = s;
        }
        else if constexpr (std::is_fundamental<T>::value)
        {
            T d {};
            in.read(reinterpret_cast<char*>(&d), sizeof(T));

            // std::cout << __FILE__ << " " << __LINE__ << " " << d << std::endl;
            node = d;
        }
        else if constexpr (is_custom_serializable_v<Config, T>)
        {
            // std::cout << __FILE__ << " " << __LINE__ << std::endl;

            auto const loader = (*this).template operator()();
            T t;
            bool success = _custom_deserialize(loader, t);
            if (!success)
                throw std::runtime_error("deserialize failed!");

            // std::cout << __FILE__ << " " << __LINE__ << " " << t.a << std::endl;
            node = t;
        }
        else
        {
            throw std::runtime_error("_deserialize_data failed");
        }
    }

    template <std::size_t I>
    void _read(Dict& node, int& found, std::size_t const& type)
    {
        using El = std::tuple_element_t<I, Tuple>;

        if constexpr (Dict::template is_value_v<El>)
            if (I + base == type)
            {
                std::cout << __FILE__ << " " << __LINE__ << " " << type << std::endl;
                _deserialize_data<El>(node);
                found += 1;
            }
    }

    template <std::size_t... Is>
    void read(Dict& node, std::size_t const& type, std::integer_sequence<std::size_t, Is...>)
    {
        int found = 0;
        (_read<Is>(node, found, type), ...);
        if (found != 1)
            throw std::runtime_error("_deserialize_data _read failed");
    }


    std::string const filename;
    std::ifstream in { filename, std::ios::binary };
};


} // namespace cppdict


#endif // CPPDICT_DICT_EXP_SERIALZIER_HPP
