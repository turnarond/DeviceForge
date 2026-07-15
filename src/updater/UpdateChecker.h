/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: UpdateChecker.h
 * Date: 2026-07-15
 * Author: turnarond
 * Description: 在线更新服务 — GitHub Releases API 查询 + zip 下载 + 校验 + 解压
 */
#pragma once
#include "lwserverbase/core/ServiceTask.h"
#include "UpdateTypes.h"
#include <curl/curl.h>
#include <functional>
#include <string>
#include <atomic>
#include <QtConcurrent>

// UpdateChecker — 设备在线更新 ServiceTask
// 状态机: idle → checking → ready / error
//        idle → downloading → installed / error
// 线程模型: 所有 IO（HTTP 查询 / 下载 / 解压）通过 QtConcurrent::run 在工作线程执行,
//          UI 回调通过 setStateChangedCallback / setProgressCallback / setErrorCallback 暴露,
//          由调用方（Task 4/5 Widget）通过 QMetaObject::invokeMethod 切回主线程。
class UpdateChecker : public lwserverbase::core::ServiceTask {
public:
    UpdateChecker();
    ~UpdateChecker() override;

    // ServiceTask 线程入口（本服务不使用激活线程,仅作为可停止语义;业务用 QtConcurrent 跑）
    int svc() override;

    // 触发异步查询 GitHub Releases latest
    void checkForUpdate();
    // 触发异步下载并解压当前 m_releaseInfo 中保存的 zip
    void downloadUpdate();
    // 取消正在进行的下载
    void cancelDownload();
    // 安装（解压后,启动新进程 + 退当前进程,占位,Task 3 完整实现）
    void installUpdate();

    UpdateState state() const { return m_state.load(); }
    const ReleaseInfo& releaseInfo() const { return m_releaseInfo; }

    void setStateChangedCallback(std::function<void(UpdateState)> cb);
    void setProgressCallback(std::function<void(int, int64_t, int64_t)> cb);
    void setErrorCallback(std::function<void(const std::string&)> cb);

    // 当前进程版本（用于版本比较,默认 "2.1.0" 可由外部 setter 覆盖）
    void setCurrentVersion(const std::string& v) { m_currentVersion = v; }
    const std::string& currentVersion() const { return m_currentVersion; }

    // GitHub 仓库坐标（默认 DeviceForge / v2.x;Task 4 改为配置）
    void setRepo(const std::string& ownerRepo) { m_repo = ownerRepo; }
    const std::string& repo() const { return m_repo; }

private:
    // 业务主流程
    ReleaseInfo fetchReleaseInfo(std::string& errorOut);
    bool downloadZip(const ReleaseInfo& info, std::string& errorOut);
    bool extractZip(const std::string& zipPath, const std::string& destDir,
                    std::string& errorOut);
    void setState(UpdateState s);
    void reportError(const std::string& msg);

    // libcurl 回调
    static size_t curlWriteCb(void* ptr, size_t size, size_t nmemb, void* userdata);
    static int curlProgressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow);

    // 路径辅助
    static std::string installDir();
    static std::string tempDir();
    static bool ensureDir(const std::string& dir);

    // 状态
    std::atomic<UpdateState> m_state{UpdateState::Idle};
    ReleaseInfo m_releaseInfo;
    std::atomic<bool> m_cancelled{false};
    std::string m_zipPath;
    std::string m_extractDir;
    std::string m_currentVersion{"2.1.0"};
    std::string m_repo{"DeviceForge/DeviceForge"};

    // 回调
    std::function<void(UpdateState)> m_stateCb;
    std::function<void(int, int64_t, int64_t)> m_progressCb;
    std::function<void(const std::string&)> m_errorCb;

    // 异步任务句柄（用于取消 / 等待完成）
    QFuture<void> m_checkFuture;
    QFuture<void> m_downloadFuture;
};
