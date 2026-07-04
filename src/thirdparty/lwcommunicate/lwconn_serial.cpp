/*
* Copyright (c) 2026 ACOAUTO Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwconn_serial.cpp .
*
* Date: 2026-02-05
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwconn_serial.h"
#include "lwconn_base.h"
#include <cstring>
#include <cstdarg>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef ERROR
#undef DEBUG
#undef WARNING
#undef INFO
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#endif

// 串口连接类实现
LWSerial::LWSerial(const std::string& name, const std::string& port, int baudrate, char parity, int databits, int stopbits)
    : LWConnBase(LWConnType::SERIAL, name),
      port_(port),
      baudrate_(baudrate),
      parity_(parity),
      databits_(databits),
      stopbits_(stopbits),
      serial_handle_(LWCONN_INVALID_FD) {
}

LWSerial::~LWSerial() {
    stop();
}

LWConnError LWSerial::start() {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    
    if (serial_handle_.valid()) {
        return LWConnError::SUCCESS;
    }

    this->updateStatus(LWConnStatus::CONNECTING);
    
#ifdef _WIN32
    // Windows 串口打开
    std::string win_port = "\\\\.\\" + port_;
    HANDLE hSerial = CreateFileA(win_port.c_str(),
                                  GENERIC_READ | GENERIC_WRITE,
                                  0,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        this->log(LWLogLevel::ERROR, "LWSerial::start: CreateFile failed: %s", port_.c_str());
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    // 配置串口参数
    DCB dcb;
    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    
    if (!GetCommState(hSerial, &dcb)) {
        CloseHandle(hSerial);
        this->log(LWLogLevel::ERROR, "LWSerial::start: GetCommState failed");
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    // 设置波特率
    switch (baudrate_) {
        case 9600:
            dcb.BaudRate = CBR_9600;
            break;
        case 19200:
            dcb.BaudRate = CBR_19200;
            break;
        case 38400:
            dcb.BaudRate = CBR_38400;
            break;
        case 57600:
            dcb.BaudRate = CBR_57600;
            break;
        case 115200:
            dcb.BaudRate = CBR_115200;
            break;
        default:
            dcb.BaudRate = CBR_9600;
            break;
    }
    
    // 设置数据位
    dcb.ByteSize = (BYTE)databits_;
    
    // 设置奇偶校验
    switch (parity_) {
        case 'O':
            dcb.Parity = ODDPARITY;
            break;
        case 'E':
            dcb.Parity = EVENPARITY;
            break;
        case 'N':
        default:
            dcb.Parity = NOPARITY;
            break;
    }
    
    // 设置停止位
    dcb.StopBits = (stopbits_ == 2) ? TWOSTOPBITS : ONESTOPBIT;
    
    if (!SetCommState(hSerial, &dcb)) {
        CloseHandle(hSerial);
        this->log(LWLogLevel::ERROR, "LWSerial::start: SetCommState failed");
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    // 设置超时
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(hSerial, &timeouts);

    serial_handle_.reset(hSerial);
#else
    // Linux/Unix 串口打开
    int fd = open(port_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        this->log(LWLogLevel::ERROR, "LWSerial::start: open failed: %s", port_.c_str());
        this->updateStatus(LWConnStatus::ERROR);
        return LWConnError::CONNECTION_FAILED;
    }

    // 配置串口
    struct termios options;
    tcgetattr(fd, &options);
    
    // 设置波特率
    speed_t speed;
    switch (baudrate_) {
        case 9600:
            speed = B9600;
            break;
        case 19200:
            speed = B19200;
            break;
        case 38400:
            speed = B38400;
            break;
        case 57600:
            speed = B57600;
            break;
        case 115200:
            speed = B115200;
            break;
        default:
            speed = B9600;
            break;
    }
    
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    // 设置数据位
    options.c_cflag &= ~CSIZE;
    switch (databits_) {
        case 7:
            options.c_cflag |= CS7;
            break;
        case 8:
        default:
            options.c_cflag |= CS8;
            break;
    }
    
    // 设置奇偶校验
    switch (parity_) {
        case 'O':
            options.c_cflag |= (PARODD | PARENB);
            options.c_iflag |= INPCK;
            break;
        case 'E':
            options.c_cflag |= PARENB;
            options.c_cflag &= ~PARODD;
            options.c_iflag |= INPCK;
            break;
        case 'N':
        default:
            options.c_cflag &= ~PARENB;
            break;
    }
    
    // 设置停止位
    if (stopbits_ == 2) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }
    
    // 应用配置
    tcsetattr(fd, TCSANOW, &options);
    
    serial_handle_.reset(fd);
#endif
    
    this->updateStatus(LWConnStatus::CONNECTED);
    this->notifyConnChange(true, port_);
    
    this->log(LWLogLevel::DEBUG, "LWSerial::start: opened: %s, baudrate: %d", port_.c_str(), baudrate_);
    return LWConnError::SUCCESS;
}

void LWSerial::stop() {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    
    if (serial_handle_.valid()) {
        serial_handle_.reset();
        this->updateStatus(LWConnStatus::DISCONNECTED);
        this->notifyConnChange(false, "");
        this->log(LWLogLevel::DEBUG, "LWSerial::stop: closed: %s", port_.c_str());
    }
}

LWConnError LWSerial::send(const char* data, size_t length, int timeout_ms) {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    
    if (!serial_handle_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

#ifdef _WIN32
    HANDLE hSerial = (HANDLE)serial_handle_.get();
    DWORD written;
    if (!WriteFile(hSerial, data, (DWORD)length, &written, NULL)) {
        this->log(LWLogLevel::ERROR, "LWSerial::send: WriteFile failed");
        return LWConnError::SEND_FAILED;
    }
    if ((size_t)written != length) {
        this->log(LWLogLevel::ERROR, "LWSerial::send: partial write");
        return LWConnError::SEND_FAILED;
    }
#else
    int fd = serial_handle_.get();
    ssize_t sent = write(fd, data, length);
    if (sent < 0) {
        this->log(LWLogLevel::ERROR, "LWSerial::send: write failed");
        return LWConnError::SEND_FAILED;
    }
    if ((size_t)sent != length) {
        this->log(LWLogLevel::ERROR, "LWSerial::send: partial write");
        return LWConnError::SEND_FAILED;
    }
#endif

    return LWConnError::SUCCESS;
}

LWConnError LWSerial::receive(char* buffer, size_t buffer_size, size_t& received_size, int timeout_ms) {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    
    if (!serial_handle_.valid()) {
        return LWConnError::NOT_CONNECTED;
    }

#ifdef _WIN32
    HANDLE hSerial = (HANDLE)serial_handle_.get();

    if (timeout_ms >= 0) {
        // 通过 COMMTIMEOUTS 实现超时等待, 避免 busy-wait 轮询
        // ReadIntervalTimeout=MAXDWORD: 有数据立即返回
        // ReadTotalTimeoutConstant: 无数据时等待 timeout_ms 后返回
        COMMTIMEOUTS cto;
        cto.ReadIntervalTimeout = MAXDWORD;
        cto.ReadTotalTimeoutMultiplier = 0;
        cto.ReadTotalTimeoutConstant = (timeout_ms > 0) ? static_cast<DWORD>(timeout_ms) : 0;
        cto.WriteTotalTimeoutMultiplier = 0;
        cto.WriteTotalTimeoutConstant = 0;
        SetCommTimeouts(hSerial, &cto);
    }

    DWORD read;
    if (!ReadFile(hSerial, buffer, static_cast<DWORD>(buffer_size), &read, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT) {
            // 恢复默认超时 (非阻塞模式)
            if (timeout_ms >= 0) {
                COMMTIMEOUTS cto;
                cto.ReadIntervalTimeout = MAXDWORD;
                cto.ReadTotalTimeoutMultiplier = 0;
                cto.ReadTotalTimeoutConstant = 0;
                cto.WriteTotalTimeoutMultiplier = 0;
                cto.WriteTotalTimeoutConstant = 0;
                SetCommTimeouts(hSerial, &cto);
            }
            return LWConnError::TIMEOUT;
        }
        this->log(LWLogLevel::ERROR, "LWSerial::receive: ReadFile failed");
        return LWConnError::RECEIVE_FAILED;
    }
    if (read == 0 && timeout_ms >= 0) {
        // 恢复默认超时 (非阻塞模式)
        COMMTIMEOUTS cto;
        cto.ReadIntervalTimeout = MAXDWORD;
        cto.ReadTotalTimeoutMultiplier = 0;
        cto.ReadTotalTimeoutConstant = 0;
        cto.WriteTotalTimeoutMultiplier = 0;
        cto.WriteTotalTimeoutConstant = 0;
        SetCommTimeouts(hSerial, &cto);
        return LWConnError::TIMEOUT;
    }
    received_size = static_cast<size_t>(read);
#else
    int fd = serial_handle_.get();
    
    if (timeout_ms == 0) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        struct timeval tv = {0, 0};
        int result = select(fd + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    } else if (timeout_ms > 0) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int result = select(fd + 1, &read_set, nullptr, nullptr, &tv);
        if (result < 0) return LWConnError::RECEIVE_FAILED;
        else if (result == 0) return LWConnError::TIMEOUT;
    }

    ssize_t received = read(fd, buffer, buffer_size);
    if (received < 0) {
        this->log(LWLogLevel::ERROR, "LWSerial::receive: read failed");
        return LWConnError::RECEIVE_FAILED;
    }

    received_size = (size_t)received;
#endif
    
    return LWConnError::SUCCESS;
}

LWConnError LWSerial::disconnect() {
    stop();
    return LWConnError::SUCCESS;
}

bool LWSerial::isConnected() const {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    return serial_handle_.valid();
}

void LWSerial::clearReceiveBuffer() {
    std::lock_guard<std::mutex> lock(serial_mutex_);

    if (serial_handle_.valid()) {
#ifdef _WIN32
        PurgeComm((HANDLE)serial_handle_.get(), PURGE_RXCLEAR);
#else
        int fd = serial_handle_.get();
        tcflush(fd, TCIFLUSH);
#endif
    }
}

int LWSerial::nativeHandle() const {
    std::lock_guard<std::mutex> lock(serial_mutex_);
#ifdef _WIN32
    // HANDLE 在 64-bit Windows 上为 8 字节, 但实际句柄值远小于 2^32,
    // 通过 intptr_t 中转安全截断到 int (基类接口约束返回值必须为 int)
    return static_cast<int>(reinterpret_cast<intptr_t>(serial_handle_.get()));
#else
    return serial_handle_.get();
#endif
}