#pragma once
#include <optional>
#include <string>
#include <vector>

namespace mxr {
struct Upstream { std::string match, url, name; };
struct ProxyCfg { std::vector<Upstream> upstreams; bool injectUsage = true; };
struct Config   { ProxyCfg proxy; int port = 8080; std::string root = "."; };

std::optional<Config> loadConfig(const std::string& path);
std::string toJsonString(const Config& cfg);
}
