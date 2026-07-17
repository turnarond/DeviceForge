/*
 * Copyright (c) 2026 ACOAUTO Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwconn_internal.h
 *
 * Date: 2026-05-21
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 */

#ifndef LWCONN_INTERNAL_H
#define LWCONN_INTERNAL_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define LWCONN_CLOSE_SOCKET(fd) closesocket(fd)
#define LWCONN_INVALID_SOCKET INVALID_SOCKET
typedef SOCKET lwconn_socket_t;
#else
#include <unistd.h>
#include <sys/socket.h>
#define LWCONN_CLOSE_SOCKET(fd) close(fd)
#define LWCONN_INVALID_SOCKET (-1)
typedef int lwconn_socket_t;
#endif

namespace lwcomm {
namespace internal {

class ScopedSocket {
    lwconn_socket_t fd_;
public:
    explicit ScopedSocket(lwconn_socket_t fd = LWCONN_INVALID_SOCKET) noexcept : fd_(fd) {}
    ~ScopedSocket() noexcept { reset(); }

    ScopedSocket(ScopedSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = LWCONN_INVALID_SOCKET; }
    ScopedSocket& operator=(ScopedSocket&& o) noexcept {
        if (this != &o) { reset(); fd_ = o.fd_; o.fd_ = LWCONN_INVALID_SOCKET; }
        return *this;
    }
    ScopedSocket(const ScopedSocket&) = delete;
    ScopedSocket& operator=(const ScopedSocket&) = delete;

    lwconn_socket_t get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ != LWCONN_INVALID_SOCKET; }

    void reset(lwconn_socket_t fd = LWCONN_INVALID_SOCKET) noexcept {
        if (fd_ != LWCONN_INVALID_SOCKET) LWCONN_CLOSE_SOCKET(fd_);
        fd_ = fd;
    }
    lwconn_socket_t release() noexcept { lwconn_socket_t f = fd_; fd_ = LWCONN_INVALID_SOCKET; return f; }
};

#ifdef _WIN32
class ScopedHandle {
    HANDLE h_;
public:
    explicit ScopedHandle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    ~ScopedHandle() noexcept { reset(); }

    ScopedHandle(ScopedHandle&& o) noexcept : h_(o.h_) { o.h_ = INVALID_HANDLE_VALUE; }
    ScopedHandle& operator=(ScopedHandle&& o) noexcept {
        if (this != &o) { reset(); h_ = o.h_; o.h_ = INVALID_HANDLE_VALUE; }
        return *this;
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    HANDLE get() const noexcept { return h_; }
    bool valid() const noexcept { return h_ != INVALID_HANDLE_VALUE; }

    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = h;
    }
    HANDLE release() noexcept { HANDLE h = h_; h_ = INVALID_HANDLE_VALUE; return h; }
};
#endif

} // namespace internal
} // namespace lwcomm

// Backward compatibility for ScopedFd (used by socket-based connections)
namespace lwcomm {
namespace internal {
typedef ScopedSocket ScopedFd;

#ifdef _WIN32
typedef HANDLE lwconn_fd_t;
#define LWCONN_INVALID_FD INVALID_HANDLE_VALUE
#define LWCONN_CLOSE_FD(fd) CloseHandle(fd)
#else
typedef int lwconn_fd_t;
#define LWCONN_INVALID_FD (-1)
#define LWCONN_CLOSE_FD(fd) close(fd)
#endif

}
}

#endif // LWCONN_INTERNAL_H
