/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: comm_impl.h .
 *
 * Date: 2024-03-23
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef _LW_COMM_IMPL_H_
#define _LW_COMM_IMPL_H_

class CLWCommImpl
{
public:
    const char *GetHomePath(void);
    const char *GetConfigPath(void);
    const char *GetBinPath(void);
    const char *GetLibPath(void);
    const char *GetModelPath(void);
    const char *GetDataPath(void);
    const char *GetImagePath(void);
    const char *GetLogPath(void);
    const char *GetProcessName(void);
    const char *GetRunTimeDataPath(void);
    void Sleep(unsigned nMilSeconds);

  public:
    CLWCommImpl();
};

#endif // _LW_COMM_IMPL_H_

/*
 * end
 */