// src/framework/ToolRegistry.cpp
#include "ToolRegistry.h"
#include "adapter/ProtocolRegistry.h"
#include "lwcomm.h"
#include "lwlog/lwlog.h"
#include <algorithm>

ToolRegistry* ToolRegistry::instance()
{
    static ToolRegistry reg;
    return &reg;
}

void ToolRegistry::registerBuiltin(std::string id, std::string name,
                                    std::string category, std::string iconPath,
                                    std::string version, std::string description)
{
    ToolEntry entry;
    entry.id          = std::move(id);
    entry.name        = std::move(name);
    entry.category    = std::move(category);
    entry.iconPath    = std::move(iconPath);
    entry.version     = std::move(version);
    entry.description = std::move(description);
    entry.isBuiltin   = true;
    entry.isAvailable = true;   // 内置 Tool 总是可用
    addEntry(std::move(entry));
}

void ToolRegistry::scanPluginDirectory(const std::string& pluginDir)
{
    // 使用 lwcomm 文件系统工具扫描子目录
    auto entries = LWFileSystem::ListDirectory(pluginDir);
    for (const auto& entry : entries) {
        // 只处理 manifest.xml 所在的子目录
        if (!entry.is_directory)
            continue;

        std::string manifestPath = LWFileSystem::ConcatPath(entry.path, "manifest.xml");

        auto manifest = ManifestParser::parse(manifestPath);
        if (!manifest.valid) {
            LWLOG_W(("跳过无效插件清单: " + manifestPath
                     + " — " + manifest.parseError).c_str());
            continue;
        }

        ToolEntry te;
        te.id          = manifest.id;
        te.name        = manifest.name;
        te.category    = manifest.category;
        te.iconPath    = manifest.icon;
        te.version     = manifest.version;
        te.description = manifest.description;
        te.isBuiltin   = false;
        te.manifest    = manifest;

        // 校验协议依赖可用性
        bool depsOK = true;
        for (const auto& dep : manifest.protocolDeps) {
            if (!ProtocolRegistry::instance()->isRegistered(dep.id)) {
                depsOK = false;
                te.unavailabilityReason = "缺少协议适配器: " + dep.id;
                break;
            }
        }
        te.isAvailable = depsOK;

        addEntry(std::move(te));
        LWLOG_I(("发现插件: " + te.id + " (" + te.name + ")").c_str());
    }
}

void ToolRegistry::addEntry(ToolEntry entry)
{
    // 去重：如果已有同名 id，以最后注册的为准
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const ToolEntry& e) { return e.id == entry.id; });
    if (it != m_entries.end()) {
        *it = std::move(entry);
    } else {
        m_entries.push_back(std::move(entry));
    }
}

std::vector<ToolRegistry::ToolEntry> ToolRegistry::listAllTools() const
{
    return m_entries;
}

std::vector<ToolRegistry::ToolEntry> ToolRegistry::listByCategory(
    const std::string& category) const
{
    std::vector<ToolEntry> result;
    for (const auto& e : m_entries) {
        if (e.category == category) {
            result.push_back(e);
        }
    }
    return result;
}

const ToolRegistry::ToolEntry* ToolRegistry::findById(const std::string& id) const
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const ToolEntry& e) { return e.id == id; });
    if (it != m_entries.end()) {
        return &(*it);
    }
    return nullptr;
}

std::vector<std::string> ToolRegistry::allCategories() const
{
    std::vector<std::string> cats;
    for (const auto& e : m_entries) {
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end()) {
            cats.push_back(e.category);
        }
    }
    return cats;
}
