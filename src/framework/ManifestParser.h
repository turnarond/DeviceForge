// src/framework/ManifestParser.h
#pragma once
#include <string>
#include <vector>
#include <optional>

// 插件清单解析结果
struct ToolManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string category;
    std::string icon;
    std::string description;
    std::string author;

    // 协议依赖
    struct ProtocolDependency {
        std::string id;
        std::string minVersion;
    };
    std::vector<ProtocolDependency> protocolDeps;

    // 宿主版本要求
    std::string minHostVersion;

    // 入口点
    struct EntryPoint {
        std::string factory;   // 工厂函数名 "createToolBackend"
        std::string library;   // DLL 文件名 "ftp_deploy_backend.dll"
    };
    std::optional<EntryPoint> backendEntry;
    std::optional<EntryPoint> widgetEntry;

    // 默认配置
    struct DefaultProperty {
        std::string key;
        std::string value;
        std::string type;  // "string" | "bool" | "int" | "double"
    };
    std::vector<DefaultProperty> defaultProperties;

    // 解析状态
    bool valid = false;
    std::string parseError;
};

// XML 插件清单解析器
class ManifestParser {
public:
    // 从文件路径解析 manifest.xml，返回 ToolManifest
    static ToolManifest parse(const std::string& xmlFilePath);

    // 从内存字符串解析
    static ToolManifest parseString(const std::string& xmlContent,
                                     const std::string& sourceName = "<memory>");
};
