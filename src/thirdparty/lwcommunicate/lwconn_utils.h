/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_utils.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_UTILS_H
#define LWCONN_UTILS_H

#include "lwconn_base.h"
#include <string>

// 工具函数
std::string lwconnErrorToString(LWConnError error);
std::string lwconnStatusToString(LWConnStatus status);
std::string lwconnTypeToString(LWConnType type);

#endif // LWCONN_UTILS_H