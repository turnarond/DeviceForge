/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ConfigManager.h
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <variant>
#include <cstdint>

namespace lwserverbase {
namespace config {

/**
 * @brief 配置值类型，支持 int64/double/bool/string
 *
 * 替代原有的全字符串存储，保留 JSON 解析时的原始类型。
 * Get<T>() 通过 std::get_if 实现零开销读取，无需每次做字符串解析。
 */
using ConfigValue = std::variant<std::monostate, int64_t, double, bool, std::string>;

/**
 * @class ConfigManager
 * @brief 配置管理器，负责配置文件的加载、保存和管理
 *
 * ConfigManager 提供了配置文件的加载、保存、获取和设置功能，
 * 支持配置热更新和配置变更通知。
 */
class ConfigManager {
public:
    /**
     * @brief 构造函数
     */
    ConfigManager();

    /**
     * @brief 析构函数
     */
    ~ConfigManager();

    /**
     * @brief 加载配置文件
     * @param configPath 配置文件路径
     * @return true 表示加载成功，false 表示失败
     */
    bool Load(const std::string& configPath);

    /**
     * @brief 保存配置文件
     * @param configPath 配置文件路径
     * @return true 表示保存成功，false 表示失败
     */
    bool Save(const std::string& configPath);

    /**
     * @brief 获取配置值
     * @tparam T 配置值类型 (string, int, double, bool, int64_t)
     * @param key 配置键
     * @param defaultValue 默认值
     * @return 配置值
     */
    template <typename T>
    T Get(const std::string& key, const T& defaultValue = T{}) const;

    /**
     * @brief 设置配置值
     * @tparam T 配置值类型
     * @param key 配置键
     * @param value 配置值
     */
    template <typename T>
    void Set(const std::string& key, const T& value);

    /**
     * @brief 注册配置变更监听器
     * @param key 配置键
     * @param handler 变更处理函数
     */
    using ConfigChangeHandler = std::function<void(const std::string& key, const std::string& oldValue, const std::string& newValue)>;
    void RegisterChangeHandler(const std::string& key, ConfigChangeHandler handler);

    /**
     * @brief 启用配置热更新
     * @param configPath 配置文件路径
     * @param interval 检查间隔（毫秒）
     */
    void EnableHotReload(const std::string& configPath, int interval = 1000);
    void DisableHotReload();

    /** 注册热更新全局回调（文件变更后触发，可在此调用 OnReloadConfig） */
    void SetReloadCallback(std::function<void()> cb);

private:
    /**
     * @brief 解析配置文件
     * @param configPath 配置文件路径
     * @return true 表示解析成功，false 表示失败
     */
    bool ParseConfigFile(const std::string& configPath);
    bool ParseJsonConfigFile(const std::string& content, std::map<std::string, ConfigValue>& outConfig);
    bool ParseKeyValueConfigFile(const std::string& content, std::map<std::string, ConfigValue>& outConfig);

    /**
     * @brief 监控配置文件变化
     */
    void MonitorConfigChanges();

    /**
     * @brief 触发配置变更通知
     * @param key 配置键
     * @param oldValue 旧值
     * @param newValue 新值
     */
    void NotifyConfigChanges(const std::string& key, const std::string& oldValue, const std::string& newValue);

    /** 将 ConfigValue 转换为字符串表示（用于变更通知） */
    static std::string ConfigValueToString(const ConfigValue& v);

private:
    std::map<std::string, ConfigValue> m_configMap;       ///< 配置键值对（类型感知）
    std::map<std::string, std::vector<ConfigChangeHandler>> m_changeHandlers; ///< 配置变更监听器
    std::string m_configPath;                              ///< 配置文件路径
    int m_hotReloadInterval;                               ///< 热更新检查间隔（毫秒）
    std::atomic<bool> m_hotReloadEnabled;                  ///< 是否启用热更新
    mutable std::mutex m_mutex;                            ///< 互斥锁（mutable 允许在 const 方法中使用）
    int64_t m_lastModTime;                                 ///< 配置文件最后修改时间
    std::thread m_hotReloadThread;                         ///< 热更新线程

    std::string m_rawJsonContent;                          ///< 原始 JSON 内容
    bool m_isJsonFormat = false;
    bool m_modified = false;
    std::function<void()> m_reloadCallback;                ///< 热更新后全局回调（可触发 OnReloadConfig）
};

// ==================== 模板方法实现 ====================

inline std::string ConfigManager::ConfigValueToString(const ConfigValue& v) {
    if (auto* p = std::get_if<int64_t>(&v))
        return std::to_string(*p);
    if (auto* p = std::get_if<double>(&v))
        return std::to_string(*p);
    if (auto* p = std::get_if<bool>(&v))
        return *p ? "true" : "false";
    if (auto* p = std::get_if<std::string>(&v))
        return *p;
    return "";
}

template <typename T>
T ConfigManager::Get(const std::string& key, const T& defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_configMap.find(key);
    if (it != m_configMap.end()) {
        // 直接从 variant 读取，零开销 — 无需字符串解析
        if constexpr (std::is_same_v<T, std::string>) {
            if (auto* p = std::get_if<std::string>(&it->second))
                return *p;
            // fallback: 其他类型转 string
            return ConfigValueToString(it->second);
        } else if constexpr (std::is_same_v<T, int>) {
            if (auto* p = std::get_if<int64_t>(&it->second))
                return static_cast<int>(*p);
            if (auto* p = std::get_if<double>(&it->second))
                return static_cast<int>(*p);
            if (auto* p = std::get_if<bool>(&it->second))
                return *p ? 1 : 0;
            if (auto* p = std::get_if<std::string>(&it->second))
                try { return std::stoi(*p); } catch (...) {}
        } else if constexpr (std::is_same_v<T, int64_t>) {
            if (auto* p = std::get_if<int64_t>(&it->second))
                return *p;
            if (auto* p = std::get_if<double>(&it->second))
                return static_cast<int64_t>(*p);
            if (auto* p = std::get_if<std::string>(&it->second))
                try { return std::stoll(*p); } catch (...) {}
        } else if constexpr (std::is_same_v<T, double>) {
            if (auto* p = std::get_if<double>(&it->second))
                return *p;
            if (auto* p = std::get_if<int64_t>(&it->second))
                return static_cast<double>(*p);
            if (auto* p = std::get_if<std::string>(&it->second))
                try { return std::stod(*p); } catch (...) {}
        } else if constexpr (std::is_same_v<T, bool>) {
            if (auto* p = std::get_if<bool>(&it->second))
                return *p;
            if (auto* p = std::get_if<int64_t>(&it->second))
                return *p != 0;
            if (auto* p = std::get_if<std::string>(&it->second))
                return *p == "true" || *p == "1";
        }
    }
    return defaultValue;
}

template <typename T>
void ConfigManager::Set(const std::string& key, const T& value) {
    std::string oldValue;
    ConfigValue newCv;

    // 构造 ConfigValue
    if constexpr (std::is_same_v<T, std::string>) {
        newCv = value;
    } else if constexpr (std::is_same_v<T, int>) {
        newCv = static_cast<int64_t>(value);
    } else if constexpr (std::is_same_v<T, int64_t>) {
        newCv = value;
    } else if constexpr (std::is_same_v<T, double>) {
        newCv = value;
    } else if constexpr (std::is_same_v<T, bool>) {
        newCv = value;
    }

    std::string newValue = ConfigValueToString(newCv);
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_configMap.find(key);
        oldValue = (it != m_configMap.end()) ? ConfigValueToString(it->second) : "";
        changed = (oldValue != newValue);
        m_configMap[key] = newCv;
        m_modified = true;
    }
    if (changed) {
        NotifyConfigChanges(key, oldValue, newValue);
    }
}

} // namespace config
} // namespace lwserverbase
