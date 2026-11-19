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
#include "io/json.hpp"
#include "io/yaml.hpp"
#include <dict.hpp>
#include <filesystem>


namespace cppconfig
{
using Config = cppdict::Dict<bool, int, double, std::string>;


inline Config from_json(const std::string& json)
{
    return config_json::load_config<Config>(json);
}

inline Config from_json(const std::filesystem::path& json_file)
{
    return config_json::load_config<Config>(json_file);
}

inline auto to_json(const Config& cfg)
{
    return config_json::save_config(cfg);
}

inline auto to_json(const std::filesystem::path& json_file, const Config& cfg)
{
    return config_json::save_config(json_file, cfg);
}

inline Config from_yaml(const std::string& yaml)
{
    return config_yaml::load_config<Config>(yaml);
}

inline Config from_yaml(const std::filesystem::path& yaml_file)
{
    return config_yaml::load_config<Config>(yaml_file);
}

}
