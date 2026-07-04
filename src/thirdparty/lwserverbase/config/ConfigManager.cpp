/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ConfigManager.cpp
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include <config/ConfigManager.h>
#include <lwcomm/lwcomm.h>
#include "yyjson.h"
#include <fstream>
#include <chrono>
#ifdef _WIN32
#include <sys/stat.h>
#define stat _stat64
#else
#include <sys/stat.h>
#endif
#include <cctype>
#include <vector>
#include <set>
#include <sstream>
#include <tuple>

namespace {

std::string Trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

/*
 * 将 yyjson 值转换为 ConfigValue。
 * 根据 JSON 实际类型存储：int→int64_t, real→double, bool→bool, str→string。
 */
static lwserverbase::config::ConfigValue yyjsonValToConfigValue(yyjson_val* v) {
    switch (yyjson_get_type(v)) {
    case YYJSON_TYPE_NUM: {
        if (yyjson_is_int(v))
            return static_cast<int64_t>(yyjson_get_sint(v));
        return yyjson_get_real(v);
    }
    case YYJSON_TYPE_BOOL:
        return yyjson_get_bool(v);
    case YYJSON_TYPE_STR:
        return std::string(yyjson_get_str(v));
    case YYJSON_TYPE_NULL:
        return std::monostate{};
    default:
        break;
    }
    return std::string{};
}

/*
 * 递归展平 JSON 对象为 dot-notation 键值对。
 * 例如 {"a":{"b":1}} → {"a.b": ConfigValue(int64_t(1))}
 */
static void flattenYyjson(yyjson_val* root, const std::string& prefix,
                          std::map<std::string, lwserverbase::config::ConfigValue>& out) {
    if (yyjson_is_obj(root)) {
        yyjson_val *key, *val;
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(root, &iter);
        while ((key = yyjson_obj_iter_next(&iter))) {
            val = yyjson_obj_iter_get_val(key);
            std::string k = yyjson_get_str(key);
            std::string full = prefix.empty() ? k : prefix + "." + k;
            if (yyjson_is_obj(val)) {
                flattenYyjson(val, full, out);
            } else {
                out[full] = yyjsonValToConfigValue(val);
            }
        }
    }
}

} // namespace

namespace lwserverbase {
namespace config {

ConfigManager::ConfigManager()
    : m_hotReloadInterval(1000)
    , m_hotReloadEnabled(false)
    , m_lastModTime(0) {
}

ConfigManager::~ConfigManager() {
    DisableHotReload();
}

bool ConfigManager::Load(const std::string& configPath) {
    m_configPath = configPath;
    return ParseConfigFile(configPath);
}

/*
 * 将 dot-notation 展平键值对重构为嵌套 JSON。
 * 例如 {"a.b": ConfigValue(int64_t(1)), "a.c": ConfigValue(string("x"))}
 *   → {"a":{"b":1,"c":"x"}}
 *
 * 使用 std::map 跟踪已创建的子对象，避免对 yyjson_mut_obj_get 的依赖。
 */
/*
 * 递归构建嵌套 JSON 字符串（不依赖 yyjson mutable API）。
 *
 * 将展平的 dot-notation 键值对按前缀分组，递归构建嵌套结构。
 * 例如 {"a.b": 1, "a.c": "x"} → "{\"a\":{\"b\":1,\"c\":\"x\"}}"
 */
static void buildNestedJson(const std::map<std::string, ConfigValue>& flat,
                            const std::string& prefix,
                            std::string& out) {
    // 收集当前层级的所有叶子键和子前缀
    std::map<std::string, ConfigValue> leaves;     // 无 "." 的键 → 值
    std::set<std::string> childPrefixes;           // 有 "." 的键的下一层前缀

    for (const auto& [k, v] : flat) {
        if (prefix.empty()) {
            auto dot = k.find('.');
            if (dot == std::string::npos) {
                leaves[k] = v;
            } else {
                childPrefixes.insert(k.substr(0, dot));
            }
        } else {
            // 只处理以 prefix + "." 开头的键
            std::string pfx = prefix + ".";
            if (k.compare(0, pfx.size(), pfx) != 0) continue;
            std::string rest = k.substr(pfx.size());
            auto dot = rest.find('.');
            if (dot == std::string::npos) {
                leaves[rest] = v;
            } else {
                childPrefixes.insert(rest.substr(0, dot));
            }
        }
    }

    out += "{";
    bool first = true;

    // 输出叶子键值对
    for (const auto& [key, val] : leaves) {
        if (!first) out += ",";
        first = false;
        out += "\"" + key + "\":";
        if (auto* p = std::get_if<int64_t>(&val)) {
            out += std::to_string(*p);
        } else if (auto* p = std::get_if<double>(&val)) {
            out += std::to_string(*p);
        } else if (auto* p = std::get_if<bool>(&val)) {
            out += (*p ? "true" : "false");
        } else if (auto* p = std::get_if<std::string>(&val)) {
            out += "\"" + *p + "\"";
        } else {
            out += "null";
        }
    }

    // 输出嵌套子对象
    for (const auto& child : childPrefixes) {
        if (!first) out += ",";
        first = false;
        std::string childPfx = prefix.empty() ? child : prefix + "." + child;
        out += "\"" + child + "\":";
        buildNestedJson(flat, childPfx, out);
    }

    out += "}";
}

static std::string buildJsonSave(const std::map<std::string, ConfigValue>& flat) {
    std::string result;
    buildNestedJson(flat, "", result);
    return result;
}

bool ConfigManager::Save(const std::string& configPath) {
    // 先拷贝数据，避免持锁做文件 I/O
    bool isJson, modified;
    std::string rawJson;
    std::map<std::string, ConfigValue> configCopy;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        isJson   = m_isJsonFormat;
        modified = m_modified;
        rawJson  = m_rawJsonContent;
        configCopy = m_configMap;
    } // ← 锁在此释放

    // 以下文件 I/O 不再持锁
    std::ofstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    if (isJson && !modified) {
        // 无修改的 JSON 格式：直接写回原始内容，保留注释和格式
        file << rawJson;
        file.close();
        return true;
    }

    if (isJson && modified) {
        file << buildJsonSave(configCopy) << '\n';
        file.close();
        // 写盘成功后标记为未修改
        std::lock_guard<std::mutex> lock(m_mutex);
        m_modified = false;
        return true;
    }

    // 原始 key=value 格式（向后兼容）
    for (const auto& [key, val] : configCopy) {
        file << key << " = " << ConfigValueToString(val) << std::endl;
    }

    file.close();
    return true;
}

void ConfigManager::RegisterChangeHandler(const std::string& key, ConfigChangeHandler handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_changeHandlers[key].push_back(handler);
}

void ConfigManager::EnableHotReload(const std::string& configPath, int interval) {
    DisableHotReload();
    m_configPath = configPath;
    m_hotReloadInterval = interval;
    m_hotReloadEnabled.store(true);
    m_hotReloadThread = std::thread([this]() { MonitorConfigChanges(); });
}

void ConfigManager::DisableHotReload() {
    m_hotReloadEnabled.store(false);
    if (m_hotReloadThread.joinable()) {
        m_hotReloadThread.join();
    }
}

bool ConfigManager::ParseConfigFile(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::map<std::string, ConfigValue> parsedConfig;
    bool parseOk = false;
    const std::string trimmed = Trim(content);
    if (!trimmed.empty() && trimmed.front() == '{') {
        parseOk = ParseJsonConfigFile(content, parsedConfig);
        if (parseOk) {
            m_rawJsonContent = content;
            m_isJsonFormat = true;
            m_modified = false;
        }
    } else {
        parseOk = ParseKeyValueConfigFile(content, parsedConfig);
        if (parseOk) {
            m_rawJsonContent.clear();
            m_isJsonFormat = false;
            m_modified = false;
        }
    }
    if (!parseOk) {
        return false;
    }

    std::vector<std::tuple<std::string, std::string, std::string>> changedItems;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto oldConfig = std::move(m_configMap);
        m_configMap = std::move(parsedConfig);

        for (const auto& [key, newVal] : m_configMap) {
            const auto it = oldConfig.find(key);
            const std::string newValue = ConfigValueToString(newVal);
            const std::string oldValue = (it != oldConfig.end()) ? ConfigValueToString(it->second) : "";
            if (oldValue != newValue) {
                changedItems.emplace_back(key, oldValue, newValue);
            }
        }
        for (const auto& [key, oldVal] : oldConfig) {
            if (m_configMap.find(key) == m_configMap.end()) {
                changedItems.emplace_back(key, ConfigValueToString(oldVal), "");
            }
        }
    }

    for (const auto& [key, oldValue, newValue] : changedItems) {
        NotifyConfigChanges(key, oldValue, newValue);
    }

    if (m_reloadCallback) {
        m_reloadCallback();
    }

    struct stat fileStat;
    if (stat(configPath.c_str(), &fileStat) == 0) {
        m_lastModTime = fileStat.st_mtime;
    }

    return true;
}

void ConfigManager::SetReloadCallback(std::function<void()> cb) {
    m_reloadCallback = std::move(cb);
}

void ConfigManager::MonitorConfigChanges() {
    while (m_hotReloadEnabled.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_hotReloadInterval));

        if (!m_hotReloadEnabled.load()) {
            break;
        }
        // 检查文件是否存在
        if (!LWFileHelper::IsFileExist(m_configPath.c_str())) {
            continue;
        }

        // 检查文件是否被修改
        struct stat fileStat;
        if (stat(m_configPath.c_str(), &fileStat) == 0 && fileStat.st_mtime > m_lastModTime) {
            ParseConfigFile(m_configPath);
        }
    }
}

void ConfigManager::NotifyConfigChanges(const std::string& key, const std::string& oldValue, const std::string& newValue) {
    std::vector<ConfigChangeHandler> handlers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_changeHandlers.find(key);
        if (it != m_changeHandlers.end()) {
            handlers = it->second;
        }
    }
    for (const auto& handler : handlers) {
        handler(key, oldValue, newValue);
    }
}

bool ConfigManager::ParseJsonConfigFile(const std::string& content,
                                         std::map<std::string, ConfigValue>& outConfig) {
    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(content.c_str()),
                                        content.size(), 0, nullptr, &err);
    if (!doc) return false;

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    outConfig.clear();
    flattenYyjson(root, "", outConfig);
    yyjson_doc_free(doc);
    return true;
}

bool ConfigManager::ParseKeyValueConfigFile(const std::string& content,
                                             std::map<std::string, ConfigValue>& outConfig) {
    outConfig.clear();
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = Trim(line.substr(0, pos));
        std::string value = Trim(line.substr(pos + 1));
        if (key.empty()) {
            continue;
        }

        // 去掉引号
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        // 启发式类型推断（与旧行为兼容）
        if (value == "true" || value == "false") {
            outConfig[key] = (value == "true");
        } else {
            bool isInt = true, isFloat = false;
            for (size_t i = 0; i < value.size(); ++i) {
                char c = value[i];
                if (i == 0 && c == '-') continue;
                if (c == '.' || c == 'e' || c == 'E') { isInt = false; isFloat = true; continue; }
                if (!std::isdigit(static_cast<unsigned char>(c))) { isInt = isFloat = false; break; }
            }
            if (isInt) {
                outConfig[key] = static_cast<int64_t>(std::stoll(value));
            } else if (isFloat) {
                outConfig[key] = std::stod(value);
            } else {
                outConfig[key] = value;
            }
        }
    }
    return true;
}

} // namespace config
} // namespace lwserverbase
