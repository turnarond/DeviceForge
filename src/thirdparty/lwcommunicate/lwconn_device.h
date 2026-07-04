/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_device.h .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#ifndef LWCONN_DEVICE_H
#define LWCONN_DEVICE_H

#include "lwconn_manager.h"
#include <string>
#include <map>

// 设备连接参数结构
typedef struct {
    // TCP客户端参数
    std::string tcp_host;
    int tcp_port;
    
    // TCP服务端参数
    std::string tcp_listen_address;
    int tcp_listen_port;
    
    // UDP参数
    int udp_local_port;
    std::string udp_remote_address;
    int udp_remote_port;
    
    // 串口参数
    std::string serial_port;
    int serial_baudrate;
    char serial_parity;
    int serial_databits;
    int serial_stopbits;
    
    // 通用参数
    int reconnect_interval;
} LWConnDeviceParams;

// 设备类
class LWConnDevice {
public:
    static std::shared_ptr<LWConnDevice> create(const std::string& device_name, 
                                              LWConnType conn_type, 
                                              const LWConnDeviceParams& params);
    
    virtual ~LWConnDevice();
    
    // 启动设备
    LWConnError start();
    
    // 停止设备
    void stop();
    
    // 发送数据
    LWConnError send(const char* data, size_t length, int timeout_ms = 0);
    
    // 接收数据
    LWConnError receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms = 0);
    
    // 设置事件回调
    void setEventHandler(LWConnEventHandler handler);
    
    // 获取设备名称
    const std::string& getName() const;
    
    // 获取连接类型
    LWConnType getConnType() const;
    
    // 获取连接状态
    LWConnStatus getStatus() const;
    
    // 获取底层连接
    std::shared_ptr<LWConnBase> getConnection();
    
    LWConnDevice(const std::string& device_name, LWConnType conn_type, const LWConnDeviceParams& params);
private:
    
    // 初始化连接
    void initConnection();
    
    std::string device_name_;
    LWConnType conn_type_;
    LWConnDeviceParams params_;
    std::shared_ptr<LWConnBase> connection_;
};

// 设备工厂类
class LWConnDeviceFactory {
public:
    static LWConnDeviceFactory& instance();
    
    // 根据连接类型创建设备
    std::shared_ptr<LWConnDevice> createDevice(const std::string& device_name, 
                                              LWConnType conn_type, 
                                              const LWConnDeviceParams& params);
    
    // 从字符串解析连接类型
    LWConnType parseConnType(const std::string& type_str);
    
    // 从整数解析连接类型
    LWConnType parseConnType(int type_id);
    
    // 解析连接参数
    LWConnDeviceParams parseConnParams(LWConnType conn_type, const std::string& conn_param_str);
    
    // 格式化连接参数
    std::string formatConnParams(LWConnType conn_type, const LWConnDeviceParams& params);
    
private:
    LWConnDeviceFactory();
    
    // 解析TCP客户端参数
    void parseTcpClientParams(const std::string& param_str, LWConnDeviceParams& params);
    
    // 解析TCP服务端参数
    void parseTcpServerParams(const std::string& param_str, LWConnDeviceParams& params);
    
    // 解析UDP参数
    void parseUdpParams(const std::string& param_str, LWConnDeviceParams& params);
    
    // 解析串口参数
    void parseSerialParams(const std::string& param_str, LWConnDeviceParams& params);
    
    std::map<std::string, LWConnType> type_str_map_;
    std::map<int, LWConnType> type_id_map_;
};

#endif // LWCONN_DEVICE_H