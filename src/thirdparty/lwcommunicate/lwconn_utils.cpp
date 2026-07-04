/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_utils.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_utils.h"

// 工具函数实现
std::string lwconnErrorToString(LWConnError error) {
    switch (error) {
        case LWConnError::SUCCESS:
            return "SUCCESS";
        case LWConnError::INVALID_PARAM:
            return "INVALID_PARAM";
        case LWConnError::CONNECTION_FAILED:
            return "CONNECTION_FAILED";
        case LWConnError::SEND_FAILED:
            return "SEND_FAILED";
        case LWConnError::RECEIVE_FAILED:
            return "RECEIVE_FAILED";
        case LWConnError::TIMEOUT:
            return "TIMEOUT";
        case LWConnError::NOT_CONNECTED:
            return "NOT_CONNECTED";
        case LWConnError::INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        default:
            return "UNKNOWN_ERROR";
    }
}

std::string lwconnStatusToString(LWConnStatus status) {
    switch (status) {
        case LWConnStatus::DISCONNECTED:
            return "DISCONNECTED";
        case LWConnStatus::CONNECTING:
            return "CONNECTING";
        case LWConnStatus::CONNECTED:
            return "CONNECTED";
        case LWConnStatus::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN_STATUS";
    }
}

std::string lwconnTypeToString(LWConnType type) {
    switch (type) {
        case LWConnType::TCP_CLIENT:
            return "TCP_CLIENT";
        case LWConnType::TCP_SERVER:
            return "TCP_SERVER";
        case LWConnType::UDP:
            return "UDP";
        case LWConnType::SERIAL:
            return "SERIAL";
        case LWConnType::UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNKNOWN_TYPE";
    }
}