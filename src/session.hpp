// A zenoh session's options from comma-separated endpoint lists and a configuration file, for programs that take them
// as strings (the programs' --connect, --listen and --zenoh-config).
#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "xblick/camera.hpp"

namespace xblick::k4a_node
{
    inline std::vector<std::string> split_endpoints(const std::string& list)
    {
        std::vector<std::string> items;
        std::stringstream in(list);
        std::string item;
        while (std::getline(in, item, ','))
        {
            if (not item.empty())
            {
                items.push_back(item);
            }
        }
        return items;
    }

    inline SessionOptions session_options(const std::string& connect, const std::string& listen, const std::string& config_file)
    {
        SessionOptions options{.connect = split_endpoints(connect), .listen = split_endpoints(listen)};
        options.config_file = config_file;
        return options;
    }
}  // namespace xblick::k4a_node
