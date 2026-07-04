/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: comm.cpp .
 *
 * Date: 2024-03-23
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "comm_impl.h"
#include "lwcomm/lwcomm.h"
#include <string>

CLWCommImpl *m_pImpl = new CLWCommImpl();

/* 
 * Constructure 
 */
LWComm::LWComm (void)
{
}

/* 
 * Destructure 
 */
LWComm::~LWComm ()
{
}

/* 
 * Get home path 
 */
const char *LWComm::GetHomePath (void)
{
    return m_pImpl->GetHomePath();
}

/* 
 * Get model path 
 */
const char *LWComm::GetModelPath (void)
{
    return m_pImpl->GetModelPath();
}

/* 
 * Get data path 
 */
const char *LWComm::GetDataPath (void)
{
    return m_pImpl->GetDataPath();
}

/* 
 * Get image path 
 */
const char *LWComm::GetImagePath (void)
{
    return m_pImpl->GetImagePath();
}

/* 
 * Get config path 
 */
const char *LWComm::GetConfigPath (void)
{
    return m_pImpl->GetConfigPath();
}

/* 
 * Get binary path 
 */
const char *LWComm::GetBinPath (void)
{
    return m_pImpl->GetBinPath();
}

/* 
 * Get lib path 
 */
const char *LWComm::GetLibPath (void)
{
    return m_pImpl->GetLibPath();
}

/* 
 * Get process path 
 */
const char *LWComm::GetProcessName (void)
{
    return m_pImpl->GetProcessName();
}

/* 
 * Get log path 
 */
const char *LWComm::GetLogPath (void)
{
    return m_pImpl->GetLogPath();
}

/* 
 * Get runtime data path 
 */
const char *LWComm::GetRunTimeDataPath (void)
{
    return m_pImpl->GetRunTimeDataPath();
}

/* 
 * Sleep
 */
void LWComm::Sleep (unsigned nMilSeconds)
{
    return m_pImpl->Sleep(nMilSeconds);
}

/*
 * end
 */
