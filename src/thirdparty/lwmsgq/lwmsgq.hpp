/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: lwmsgq.hpp
 *
 * Date: 2024-06-07
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: C++ RAII wrapper for lwmsgq message queue.
 */

#ifndef LWMSGQ_HPP_
#define LWMSGQ_HPP_

#include <cstdlib>
#include <new>

#include "lwmsgq.h"

namespace vsoa_sdk {
namespace msgq {

/**
 * @brief C++ RAII wrapper for the lwmsgq message queue.
 *
 * Provides type-safe, RAII-based message queue operations. The template
 * parameter T must be copy-constructible. Each message is heap-allocated
 * internally and freed automatically after consumption.
 *
 * Example usage:
 * @code
 *   vsoa_sdk::msgq::MessageQueue<int> mq(64);
 *   mq.push(42);
 *   int value;
 *   mq.pop(value);   // value == 42
 * @endcode
 */
template <typename T>
class MessageQueue {
public:
    using value_type = T;

    /**
     * @brief Construct a message queue with given capacity.
     * @param size Maximum number of messages the queue can hold.
     * @throws std::bad_alloc if memory allocation fails.
     */
    explicit MessageQueue(int size) {
        msgq_ = MsgQueCreate(size, nullptr, nullptr);
        if (!msgq_) {
            throw std::bad_alloc();
        }
    }

    /**
     * @brief Destroy the message queue. All pending messages are freed.
     */
    ~MessageQueue() {
        if (msgq_) {
            MsgQueRelease(&msgq_);
        }
    }

    /** Non-copyable. */
    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    /**
     * @brief Move constructor.
     */
    MessageQueue(MessageQueue&& other) noexcept
        : msgq_(other.msgq_) {
        other.msgq_ = nullptr;
    }

    /**
     * @brief Move assignment.
     */
    MessageQueue& operator=(MessageQueue&& other) noexcept {
        if (this != &other) {
            if (msgq_) {
                MsgQueRelease(&msgq_);
            }
            msgq_ = other.msgq_;
            other.msgq_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Push a message into the queue (blocking if full).
     * @param item The message to push.
     * @return true on success, false if the queue is full.
     */
    bool push(const T& item) {
        T* copy = new T(item);
        if (MsgQuePush(msgq_, copy, sizeof(T)) != 0) {
            delete copy;
            return false;
        }
        return true;
    }

    /**
     * @brief Try to push a message (non-blocking).
     * @param item The message to push.
     * @return true on success, false if the queue is full.
     */
    bool try_push(const T& item) {
        T* copy = new T(item);
        if (MsgQueTryPush(msgq_, copy, sizeof(T)) != 0) {
            delete copy;
            return false;
        }
        return true;
    }

    /**
     * @brief Push a message, evicting the oldest message if the queue is full.
     * @param item The message to push.
     * @param evicted [out] If non-null, receives the evicted message.
     * @return true on success, false on error.
     */
    bool push_with_evict(const T& item, T* evicted = nullptr) {
        T* copy = new T(item);
        void* popped = nullptr;
        if (MsgQuePushWithEvict(msgq_, copy, sizeof(T), &popped) != 0) {
            delete copy;
            return false;
        }
        if (evicted && popped) {
            *evicted = *static_cast<T*>(popped);
        }
        if (popped) {
            delete static_cast<T*>(popped);
        }
        return true;
    }

    /**
     * @brief Pop a message (blocking, waits when empty).
     * @param item [out] Receives the popped message.
     * @return true on success, false on error.
     */
    bool pop(T& item) {
        void* msg = nullptr;
        int len = 0;
        if (MsgQuePop(msgq_, &msg, &len) != 0) {
            return false;
        }
        if (msg) {
            item = *static_cast<T*>(msg);
            delete static_cast<T*>(msg);
            return true;
        }
        return false;
    }

    /**
     * @brief Try to pop a message (non-blocking).
     * @param item [out] Receives the popped message if available.
     * @return true on success, false if the queue is empty.
     */
    bool try_pop(T& item) {
        void* msg = nullptr;
        int len = 0;
        if (MsgQueTryPop(msgq_, &msg, &len) != 0) {
            return false;
        }
        if (msg) {
            item = *static_cast<T*>(msg);
            delete static_cast<T*>(msg);
            return true;
        }
        return false;
    }

    /**
     * @brief Pop a message with timeout.
     * @param item [out] Receives the popped message.
     * @param timeout_ms Timeout in milliseconds.
     * @return true on success, false on timeout or error.
     */
    bool pop_timeout(T& item, int timeout_ms) {
        void* msg = nullptr;
        int len = 0;
        if (MsgQuePopTimeout(msgq_, &msg, &len, timeout_ms) != 0) {
            return false;
        }
        if (msg) {
            item = *static_cast<T*>(msg);
            delete static_cast<T*>(msg);
            return true;
        }
        return false;
    }

    /**
     * @brief Get the number of messages currently in the queue.
     * @return Number of used slots.
     */
    int size() const {
        return MsgQueGetUsedNums(msgq_);
    }

    /**
     * @brief Check if the queue is empty.
     * @return true if empty, false otherwise.
     */
    bool empty() const {
        return size() == 0;
    }

    /**
     * @brief Check if the queue is valid.
     */
    explicit operator bool() const {
        return msgq_ != nullptr;
    }

    /**
     * @brief Get the native handle.
     */
    PLW_MSGQUE_S native_handle() {
        return msgq_;
    }

private:
    PLW_MSGQUE_S msgq_;
};

}  // namespace msgq
}  // namespace vsoa_sdk

#endif  // LWMSGQ_HPP_
