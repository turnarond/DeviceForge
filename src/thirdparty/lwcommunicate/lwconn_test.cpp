/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_test.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_manager.h"
#include "lwconn_utils.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

// 事件处理函数
void eventHandler(bool connected, const string& extra_info) {
    cout << "[Event] Connected: " << (connected ? "true" : "false") << ", Extra: " << extra_info << endl;
}

// 测试TCP客户端
void testTcpClient() {
    cout << "=== Testing TCP Client ===" << endl;
    
    auto& manager = LWConnManager::instance();
    auto client = manager.createTcpClient("test_tcp_client", "127.0.0.1", 8080);
    
    client->setEventHandler(eventHandler);
    
    // 尝试连接
    auto result = client->start();
    cout << "TCP Client start result: " << lwconnErrorToString(result) << endl;
    
    // 等待一段时间
    this_thread::sleep_for(chrono::seconds(2));
    
    // 发送数据
    const char* test_data = "Hello TCP Server!";
    result = client->send(test_data, strlen(test_data));
    cout << "TCP Client send result: " << lwconnErrorToString(result) << endl;
    
    // 接收数据
    char buffer[1024];
    size_t received_size = 0;
    result = client->receive(buffer, sizeof(buffer), received_size, 1000);
    cout << "TCP Client receive result: " << lwconnErrorToString(result);
    if (result == LWConnError::SUCCESS) {
        cout << ", Received: " << string(buffer, received_size);
    }
    cout << endl;
    
    // 停止连接
    client->stop();
    cout << "TCP Client stopped" << endl;
    cout << "=== TCP Client Test Done ===" << endl << endl;
}

// 测试TCP服务端
void testTcpServer() {
    cout << "=== Testing TCP Server ===" << endl;
    
    auto& manager = LWConnManager::instance();
    auto server = manager.createTcpServer("test_tcp_server", "0.0.0.0", 8080);
    
    server->setEventHandler(eventHandler);
    
    // 启动服务端
    auto result = server->start();
    cout << "TCP Server start result: " << lwconnErrorToString(result) << endl;
    
    // 等待客户端连接
    cout << "TCP Server waiting for connections..." << endl;
    
    // 运行一段时间
    this_thread::sleep_for(chrono::seconds(5));
    
    // 获取连接的客户端
    auto clients = server->getConnectedClients();
    cout << "Connected clients: " << clients.size() << endl;
    for (const auto& client : clients) {
        cout << "Client: " << client << endl;
    }
    
    // 停止服务端
    server->stop();
    cout << "TCP Server stopped" << endl;
    cout << "=== TCP Server Test Done ===" << endl << endl;
}

// 测试UDP
void testUdp() {
    cout << "=== Testing UDP ===" << endl;
    
    auto& manager = LWConnManager::instance();
    auto udp = manager.createUdp("test_udp", 8888, "127.0.0.1", 9999);
    
    udp->setEventHandler(eventHandler);
    
    // 启动UDP
    auto result = udp->start();
    cout << "UDP start result: " << lwconnErrorToString(result) << endl;
    
    // 发送数据
    const char* test_data = "Hello UDP!";
    result = udp->send(test_data, strlen(test_data));
    cout << "UDP send result: " << lwconnErrorToString(result) << endl;
    
    // 发送数据到指定地址
    result = udp->sendTo("127.0.0.1:9999", test_data, strlen(test_data));
    cout << "UDP sendTo result: " << lwconnErrorToString(result) << endl;
    
    // 接收数据
    char buffer[1024];
    size_t received_size = 0;
    string sender_address;
    result = udp->receiveFrom(sender_address, buffer, sizeof(buffer), received_size, 1000);
    cout << "UDP receiveFrom result: " << lwconnErrorToString(result);
    if (result == LWConnError::SUCCESS) {
        cout << ", From: " << sender_address << ", Received: " << string(buffer, received_size);
    }
    cout << endl;
    
    // 停止UDP
    udp->stop();
    cout << "UDP stopped" << endl;
    cout << "=== UDP Test Done ===" << endl << endl;
}

// 测试串口
void testSerial() {
    cout << "=== Testing Serial Port ===" << endl;
    
    auto& manager = LWConnManager::instance();
    // 注意：实际测试时需要修改串口设备名
    auto serial = manager.createSerial("test_serial", "/dev/ttyUSB0", 9600);
    
    serial->setEventHandler(eventHandler);
    
    // 尝试打开串口
    auto result = serial->start();
    cout << "Serial start result: " << lwconnErrorToString(result) << endl;
    
    // 发送数据
    const char* test_data = "Hello Serial Port!";
    result = serial->send(test_data, strlen(test_data));
    cout << "Serial send result: " << lwconnErrorToString(result) << endl;
    
    // 接收数据
    char buffer[1024];
    size_t received_size = 0;
    result = serial->receive(buffer, sizeof(buffer), received_size, 1000);
    cout << "Serial receive result: " << lwconnErrorToString(result);
    if (result == LWConnError::SUCCESS) {
        cout << ", Received: " << string(buffer, received_size);
    }
    cout << endl;
    
    // 停止串口
    serial->stop();
    cout << "Serial stopped" << endl;
    cout << "=== Serial Test Done ===" << endl << endl;
}

// 测试连接池
void testConnPool() {
    cout << "=== Testing Connection Pool ===" << endl;
    
    auto& manager = LWConnManager::instance();
    auto& pool = manager.getConnPool();
    
    // 创建多个连接
    auto client1 = manager.createTcpClient("client1", "127.0.0.1", 8080);
    auto client2 = manager.createTcpClient("client2", "127.0.0.1", 8081);
    auto udp1 = manager.createUdp("udp1", 8888);
    
    // 获取连接
    auto conn = pool.getConn("client1");
    cout << "Get connection 'client1': " << (conn ? "success" : "failed") << endl;
    
    // 获取所有连接
    auto all_conns = pool.getAllConns();
    cout << "Total connections in pool: " << all_conns.size() << endl;
    
    // 启动所有连接
    pool.startAll();
    cout << "Started all connections" << endl;
    
    // 等待一段时间
    this_thread::sleep_for(chrono::seconds(1));
    
    // 停止所有连接
    pool.stopAll();
    cout << "Stopped all connections" << endl;
    
    // 清理池
    pool.clear();
    cout << "Cleared connection pool" << endl;
    cout << "=== Connection Pool Test Done ===" << endl << endl;
}

int main() {
    cout << "=== LWCommunicate Test Suite ===" << endl << endl;
    
    // 初始化管理器
    auto& manager = LWConnManager::instance();
    manager.init();
    
    // 运行测试
    testConnPool();
    testTcpServer();
    
    // 启动TCP客户端测试（在服务端启动后）
    thread client_thread(testTcpClient);
    client_thread.join();
    
    testUdp();
    testSerial();
    
    // 反初始化
    manager.uninit();
    
    cout << "=== All Tests Done ===" << endl;
    return 0;
}