// src/framework/ManifestParser.cpp
#include "ManifestParser.h"
#include "tinyxml2.h"
#include "lwlog.h"
#include <fstream>
#include <iterator>

using namespace tinyxml2;

namespace {

    std::string safeText(XMLElement* parent, const char* childName) {
        if (!parent) return "";
        auto* el = parent->FirstChildElement(childName);
        if (!el) return "";
        const char* txt = el->GetText();
        return txt ? std::string(txt) : std::string();
    }

    std::string safeAttr(XMLElement* el, const char* attrName) {
        if (!el) return "";
        const char* val = el->Attribute(attrName);
        return val ? std::string(val) : std::string();
    }

} // namespace

ToolManifest ManifestParser::parseString(const std::string& xmlContent,
                                          const std::string& sourceName) {
    ToolManifest m;

    XMLDocument doc;
    XMLError err = doc.Parse(xmlContent.c_str());
    if (err != XML_SUCCESS) {
        m.parseError = sourceName + ": XML 解析错误: " + std::string(doc.ErrorStr());
        LWLOG_E(m.parseError.c_str());
        return m;
    }

    auto* root = doc.FirstChildElement("tool");
    if (!root) {
        m.parseError = sourceName + ": 缺少 <tool> 根元素";
        return m;
    }

    // 基本信息
    m.id = safeText(root, "id");
    m.name = safeText(root, "name");
    m.version = safeText(root, "version");
    m.category = safeText(root, "category");
    m.icon = safeText(root, "icon");
    m.description = safeText(root, "description");
    m.author = safeText(root, "author");

    if (m.id.empty() || m.name.empty()) {
        m.parseError = sourceName + ": id 和 name 为必填字段";
        return m;
    }

    // 依赖
    auto* requiresEl = root->FirstChildElement("requires");
    if (requiresEl) {
        m.minHostVersion = safeAttr(requiresEl->FirstChildElement("hostVersion"), "min");
        for (auto* proto = requiresEl->FirstChildElement("protocol");
             proto != nullptr;
             proto = proto->NextSiblingElement("protocol")) {
            ToolManifest::ProtocolDependency dep;
            dep.id = safeAttr(proto, "id");
            dep.minVersion = safeAttr(proto, "minVersion");
            if (!dep.id.empty()) {
                m.protocolDeps.push_back(dep);
            }
        }
    }

    // 入口点
    auto* backendEl = root->FirstChildElement("backend");
    if (backendEl) {
        m.backendEntry = ToolManifest::EntryPoint{
            safeAttr(backendEl, "factory"),
            safeAttr(backendEl, "library")
        };
    }

    auto* widgetEl = root->FirstChildElement("widget");
    if (widgetEl) {
        m.widgetEntry = ToolManifest::EntryPoint{
            safeAttr(widgetEl, "factory"),
            safeAttr(widgetEl, "library")
        };
    }

    // 默认配置
    auto* defaultsEl = root->FirstChildElement("defaults");
    if (defaultsEl) {
        for (auto* prop = defaultsEl->FirstChildElement("property");
             prop != nullptr;
             prop = prop->NextSiblingElement("property")) {
            ToolManifest::DefaultProperty dp;
            dp.key = safeAttr(prop, "key");
            dp.value = safeAttr(prop, "value");
            dp.type = safeAttr(prop, "type");
            if (!dp.key.empty()) {
                m.defaultProperties.push_back(dp);
            }
        }
    }

    m.valid = true;
    return m;
}

ToolManifest ManifestParser::parse(const std::string& xmlFilePath) {
    std::ifstream file(xmlFilePath);
    if (!file.is_open()) {
        ToolManifest m;
        m.parseError = "无法打开文件: " + xmlFilePath;
        return m;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parseString(content, xmlFilePath);
}
