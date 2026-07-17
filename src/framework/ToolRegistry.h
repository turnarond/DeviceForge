// src/framework/ToolRegistry.h
#pragma once
#include "ManifestParser.h"
#include <string>
#include <vector>
#include <memory>

class ToolBackend;
class ToolWidget;

// Tool 注册表 — 管理所有可用 Tool 的元数据（Meyer's Singleton）
class ToolRegistry {
public:
    static ToolRegistry* instance();

    // Tool 条目 — 由内置注册或插件扫描产生
    struct ToolEntry {
        std::string   id;
        std::string   name;
        std::string   category;
        std::string   iconPath;
        std::string   version;
        std::string   description;
        bool          isBuiltin = false;      // true=编译在主程序, false=DLL插件
        bool          isAvailable = false;    // 所有依赖满足时为 true
        std::string   unavailabilityReason;   // 不可用原因
        ToolManifest  manifest;               // 原始清单数据（插件独有）
    };

    // 注册内置 Tool（编译时绑定，无需 manifest）
    void registerBuiltin(std::string id, std::string name,
                         std::string category, std::string iconPath,
                         std::string version, std::string description);

    // 扫描 plugins/ 目录发现 DLL 插件
    // 逐个读取子目录下的 manifest.xml，解析并校验协议依赖
    void scanPluginDirectory(const std::string& pluginDir);

    // --- 查询接口 ---
    std::vector<ToolEntry> listAllTools() const;
    std::vector<ToolEntry> listByCategory(const std::string& category) const;
    const ToolEntry* findById(const std::string& id) const;
    std::vector<std::string> allCategories() const;

private:
    ToolRegistry() = default;
    std::vector<ToolEntry> m_entries;
    void addEntry(ToolEntry entry);
};
