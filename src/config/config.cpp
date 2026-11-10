#include "config.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <nlohmann/json.hpp>

namespace mxr {

namespace {   // 匿名命名空间：这两个辅助函数只在本文件可见

// 读一个标量字段；缺失→保留 out 的当前值，类型错→报错并返回 false
template <class T>
bool readScalar(const nlohmann::json& j, const char* key, T& out, const char* typeName) {
    try {
        out = j.value(key, out);      // 默认值就是 out 自己（来自成员的初始化器）
        return true;
    } catch (const nlohmann::json::type_error&) {
        std::cerr << "[config] " << key << " 类型错误，应为" << typeName << "\n";
        return false;
    }
}

// 把一个 JSON 元素转成 Upstream；任一字段缺失/类型错 → nullopt
std::optional<Upstream> parseUpstream(const nlohmann::json& el) {
    try {
        std::string match = el.value("match", std::string{});
        std::string url   = el.value("url",   std::string{});
        std::string name  = el.value("name",  std::string{});

        if (match.empty() || url.empty()) {     // ① 见下面解释
            return std::nullopt;
        }
        return Upstream{match, url, name};      // ② 隐式转成 optional<Upstream>
    } catch (const nlohmann::json::type_error&) {
        return std::nullopt;                    // ③ 类型错 = 这条规则坏
    }

}

} // namespace

std::optional<Config> loadConfig(const std::string& path) {
    // ① 打开文件
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[config] 打不开 " << path << "，使用默认配置\n";
        return Config{};                       // 情况 a：降级，注意不是 nullopt
    }

    // ② 整读成字符串（括号别丢，否则变成函数声明）
    std::string text((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());

    // ③ 解析
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[config] JSON 语法错误: " << e.what() << "\n";
        return std::nullopt;                   // 情况 b：fatal
    }

    // ④ 顶层必须是对象
    if (!j.is_object()) {
        std::cerr << "[config] 顶层不是 JSON 对象\n";
        return std::nullopt;
    }

    Config cfg;

    // 【空 2】标量字段：缺失保留结构体默认值，类型错则致命
    if (!readScalar(j, "port", cfg.port, "整数"))    return std::nullopt;
    if (!readScalar(j, "root", cfg.root, "字符串"))  return std::nullopt;

    // 【空 3】结构性字段前置检查
    // 要求: if (!j.contains("proxy")) → cerr 报错 + return std::nullopt;
    //       然后取 const auto& pj = j.at("proxy");
    //       if (!pj.contains("upstreams") || !pj.at("upstreams").is_array()) → 报错 + nullopt
    //       类型：用 const nlohmann::json& 接，不要拷贝
    if (!j.contains("proxy")) {
        std::cerr << "[config] 缺少 proxy 节点\n";
        return std::nullopt;
    }
    const auto& pj = j.at("proxy");         
    if (!pj.contains("upstreams") || !pj.at("upstreams").is_array()) {
        std::cerr << "[config] proxy.upstreams 缺失或不是数组\n";
        return std::nullopt;
    }

    // 【空 4】proxy 内的布尔
    if (!readScalar(pj, "injectUsage", cfg.proxy.injectUsage, "布尔")) return std::nullopt;
    // 【空 5】循环 upstreams
    for (const auto& el : pj.at("upstreams")) {
        auto u = parseUpstream(el);
        if (!u) { std::cerr << "[config] 跳过一条非法 upstream\n"; continue; }
        cfg.proxy.upstreams.push_back(std::move(*u));
    }
    if (cfg.proxy.upstreams.empty()){
        std::cerr<<"没有任何 upstream"<<std::endl;
        return std::nullopt;
    } 

    return cfg;
}

} // namespace mxr
