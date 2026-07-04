/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ServiceBase.cpp
 *
 * Date: 2026-03-03
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include <core/ServiceBase.h>
#include <core/ServiceManager.h>

namespace lwserverbase {
namespace core {

ServiceBase::ServiceBase() : m_serviceManager(nullptr) {
}

ServiceBase::~ServiceBase() {
}

void ServiceBase::OnReloadConfig() {
    // 默认实现为空
}

void ServiceBase::OnHealthCheck() {
    // 默认实现为空
}

bool ServiceBase::OnCommand(const std::string& /*cmd*/, const std::string& /*args*/, std::string& /*resp*/) {
    // 默认实现返回 false，表示命令未处理
    return false;
}

ServiceManager* ServiceBase::GetServiceManager() const {
    return m_serviceManager;
}

void ServiceBase::SetServiceManager(ServiceManager* manager) {
    m_serviceManager = manager;
}

std::string ServiceBase::GetServiceName() const {
    return "UnknownService";
}

std::string ServiceBase::GetServiceVersion() const {
    return "1.0.0";
}

std::string ServiceBase::GetServiceDescription() const {
    return "Unknown service";
}

} // namespace core
} // namespace lwserverbase