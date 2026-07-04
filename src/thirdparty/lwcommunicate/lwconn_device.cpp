/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_device.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_device.h"
#include <sstream>
#include "lwconn_base.h"
#include <iostream>

// 设备类实现
LWConnDevice::LWConnDevice(const std::string& device_name, LWConnType conn_type, const LWConnDeviceParams& params)
    : device_name_(device_name),
      conn_type_(conn_type),
      params_(params) {
    initConnection();
}

LWConnDevice::~LWConnDevice() {
    stop();
}

std::shared_ptr<LWConnDevice> LWConnDevice::create(const std::string& device_name, 
                                                  LWConnType conn_type, 
                                                  const LWConnDeviceParams& params) {
    return std::make_shared<LWConnDevice>(device_name, conn_type, params);
}

void LWConnDevice::initConnection() {
    switch (conn_type_) {
        case LWConnType::TCP_CLIENT:
            connection_ = CONN_MANAGER.createTcpClient(device_name_,
                                                      params_.tcp_host,
                                                      params_.tcp_port);
            break;
            
        case LWConnType::TCP_SERVER:
            connection_ = CONN_MANAGER.createTcpServer(device_name_, 
                                                      params_.tcp_listen_address, 
                                                      params_.tcp_listen_port);
            break;
            
        case LWConnType::UDP:
            connection_ = CONN_MANAGER.createUdp(device_name_, 
                                                params_.udp_local_port, 
                                                params_.udp_remote_address, 
                                                params_.udp_remote_port);
            break;
            
        case LWConnType::SERIAL:
            connection_ = CONN_MANAGER.createSerial(device_name_, 
                                                  params_.serial_port, 
                                                  params_.serial_baudrate, 
                                                  params_.serial_parity, 
                                                  params_.serial_databits, 
                                                  params_.serial_stopbits);
            break;
            
        default:
            std::cerr << "LWConnDevice::initConnection: unsupported connection type (type=" << static_cast<int>(conn_type_) << ")" << std::endl;
            break;
    }
}

LWConnError LWConnDevice::start() {
    if (connection_) {
        return connection_->start();
    }
    return LWConnError::INTERNAL_ERROR;
}

void LWConnDevice::stop() {
    if (connection_) {
        connection_->stop();
    }
}

LWConnError LWConnDevice::send(const char* data, size_t length, int timeout_ms) {
    if (connection_) {
        return connection_->send(data, length, timeout_ms);
    }
    return LWConnError::NOT_CONNECTED;
}

LWConnError LWConnDevice::receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms) {
    if (connection_) {
        return connection_->receive(buffer, buffer_size, received_size, timeout_ms);
    }
    return LWConnError::NOT_CONNECTED;
}

void LWConnDevice::setEventHandler(LWConnEventHandler handler) {
    if (connection_) {
        connection_->setEventHandler(handler);
    }
}

const std::string& LWConnDevice::getName() const {
    return device_name_;
}

LWConnType LWConnDevice::getConnType() const {
    return conn_type_;
}

LWConnStatus LWConnDevice::getStatus() const {
    if (connection_) {
        return connection_->getStatus();
    }
    return LWConnStatus::DISCONNECTED;
}

std::shared_ptr<LWConnBase> LWConnDevice::getConnection() {
    return connection_;
}

// 设备工厂类实现
LWConnDeviceFactory& LWConnDeviceFactory::instance() {
    static LWConnDeviceFactory instance;
    return instance;
}

LWConnDeviceFactory::LWConnDeviceFactory() {
    // 初始化类型映射
    type_str_map_["tcpclient"] = LWConnType::TCP_CLIENT;
    type_str_map_["tcpserver"] = LWConnType::TCP_SERVER;
    type_str_map_["udp"] = LWConnType::UDP;
    type_str_map_["serial"] = LWConnType::SERIAL;
    type_str_map_["notsupport"] = LWConnType::UNKNOWN;

    type_id_map_[1] = LWConnType::TCP_CLIENT;
    type_id_map_[2] = LWConnType::TCP_SERVER;
    type_id_map_[3] = LWConnType::UDP;
    type_id_map_[4] = LWConnType::SERIAL;
    type_id_map_[0] = LWConnType::UNKNOWN;
}

std::shared_ptr<LWConnDevice> LWConnDeviceFactory::createDevice(const std::string& device_name, 
                                                              LWConnType conn_type, 
                                                              const LWConnDeviceParams& params) {
    return LWConnDevice::create(device_name, conn_type, params);
}

LWConnType LWConnDeviceFactory::parseConnType(const std::string& type_str) {
    auto it = type_str_map_.find(type_str);
    if (it != type_str_map_.end()) {
        return it->second;
    }
    return LWConnType::UNKNOWN;
}

LWConnType LWConnDeviceFactory::parseConnType(int type_id) {
    auto it = type_id_map_.find(type_id);
    if (it != type_id_map_.end()) {
        return it->second;
    }
    return LWConnType::UNKNOWN;
}

LWConnDeviceParams LWConnDeviceFactory::parseConnParams(LWConnType conn_type, const std::string& conn_param_str) {
    LWConnDeviceParams params;
    
    switch (conn_type) {
        case LWConnType::TCP_CLIENT:
            parseTcpClientParams(conn_param_str, params);
            break;
            
        case LWConnType::TCP_SERVER:
            parseTcpServerParams(conn_param_str, params);
            break;
            
        case LWConnType::UDP:
            // 解析UDP参数
            break;
            
        case LWConnType::SERIAL:
            parseSerialParams(conn_param_str, params);
            break;
            
        default:
            break;
    }
    
    return params;
}

std::string LWConnDeviceFactory::formatConnParams(LWConnType conn_type, const LWConnDeviceParams& params) {
    std::stringstream ss;
    
    switch (conn_type) {
        case LWConnType::TCP_CLIENT:
            ss << "ip=" << params.tcp_host << ";port=" << params.tcp_port;
            break;
            
        case LWConnType::TCP_SERVER:
            ss << "listenip=" << params.tcp_listen_address << ";listenport=" << params.tcp_listen_port;
            break;
            
        case LWConnType::UDP:
            ss << "localport=" << params.udp_local_port;
            if (!params.udp_remote_address.empty()) {
                ss << ";ip=" << params.udp_remote_address << ";port=" << params.udp_remote_port;
            }
            break;
            
        case LWConnType::SERIAL:
            ss << "port=" << params.serial_port << ";baudrate=" << params.serial_baudrate;
            ss << ";parity=" << params.serial_parity;
            ss << ";databits=" << params.serial_databits;
            ss << ";stopbits=" << params.serial_stopbits;
            break;
            
        default:
            break;
    }
    
    return ss.str();
}

void LWConnDeviceFactory::parseTcpClientParams(const std::string& param_str, LWConnDeviceParams& params) {
    // 格式1: ip=192.168.1.1;port=8080
    size_t ip_pos = param_str.find("ip=");
    size_t port_pos = param_str.find("port=");

    if (ip_pos != std::string::npos && port_pos != std::string::npos) {
        size_t ip_end = param_str.find(';', ip_pos);
        if (ip_end == std::string::npos) ip_end = param_str.length();
        params.tcp_host = param_str.substr(ip_pos + 3, ip_end - (ip_pos + 3));

        size_t port_end = param_str.find(';', port_pos);
        if (port_end == std::string::npos) port_end = param_str.length();
        params.tcp_port = std::stoi(param_str.substr(port_pos + 5, port_end - (port_pos + 5)));
        return;
    }

    // 格式2: 192.168.1.1:502 (裸 ip:port)
    size_t colon = param_str.find(':');
    if (colon != std::string::npos && param_str.find(';') == std::string::npos
                                   && param_str.find('=') == std::string::npos) {
        params.tcp_host = param_str.substr(0, colon);
        params.tcp_port = std::stoi(param_str.substr(colon + 1));
        return;
    }
}

void LWConnDeviceFactory::parseTcpServerParams(const std::string& param_str, LWConnDeviceParams& params) {
    // 解析格式: listenip=192.168.1.1;listenport=8080
    size_t listenip_pos = param_str.find("listenip=");
    size_t listenport_pos = param_str.find("listenport=");
    
    if (listenip_pos != std::string::npos && listenport_pos != std::string::npos) {
        size_t listenip_end = param_str.find(';', listenip_pos);
        if (listenip_end == std::string::npos) listenip_end = param_str.length();
        params.tcp_listen_address = param_str.substr(listenip_pos + 9, listenip_end - (listenip_pos + 9));
        
        size_t listenport_end = param_str.find(';', listenport_pos);
        if (listenport_end == std::string::npos) listenport_end = param_str.length();
        params.tcp_listen_port = std::stoi(param_str.substr(listenport_pos + 11, listenport_end - (listenport_pos + 11)));
    }
}

void LWConnDeviceFactory::parseSerialParams(const std::string& param_str, LWConnDeviceParams& params) {
    // 解析格式: port=/dev/ttyUSB0;baudrate=9600;parity=N;databits=8;stopbits=1
    size_t port_pos = param_str.find("port=");
    size_t baudrate_pos = param_str.find("baudrate=");
    size_t parity_pos = param_str.find("parity=");
    size_t databits_pos = param_str.find("databits=");
    size_t stopbits_pos = param_str.find("stopbits=");
    
    if (port_pos != std::string::npos) {
        size_t port_end = param_str.find(';', port_pos);
        if (port_end == std::string::npos) port_end = param_str.length();
        params.serial_port = param_str.substr(port_pos + 5, port_end - (port_pos + 5));
    }
    
    if (baudrate_pos != std::string::npos) {
        size_t baudrate_end = param_str.find(';', baudrate_pos);
        if (baudrate_end == std::string::npos) baudrate_end = param_str.length();
        params.serial_baudrate = std::stoi(param_str.substr(baudrate_pos + 9, baudrate_end - (baudrate_pos + 9)));
    }
    
    if (parity_pos != std::string::npos) {
        size_t parity_end = param_str.find(';', parity_pos);
        if (parity_end == std::string::npos) parity_end = param_str.length();
        params.serial_parity = param_str.substr(parity_pos + 7, 1)[0];
    }
    
    if (databits_pos != std::string::npos) {
        size_t databits_end = param_str.find(';', databits_pos);
        if (databits_end == std::string::npos) databits_end = param_str.length();
        params.serial_databits = std::stoi(param_str.substr(databits_pos + 9, databits_end - (databits_pos + 9)));
    }
    
    if (stopbits_pos != std::string::npos) {
        size_t stopbits_end = param_str.find(';', stopbits_pos);
        if (stopbits_end == std::string::npos) stopbits_end = param_str.length();
        params.serial_stopbits = std::stoi(param_str.substr(stopbits_pos + 9, stopbits_end - (stopbits_pos + 9)));
    }
}