/* Copyright (c) 2025
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LOGCABIN_SERVER_METRICSSERVER_H
#define LOGCABIN_SERVER_METRICSSERVER_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "Core/ConditionVariable.h"
#include "Core/Config.h"
#include "Core/Mutex.h"

namespace LogCabin {
namespace Server {

/**
 * 提供 HTTP 端点来暴露 Prometheus 指标的服务器。
 */
class MetricsServer {
public:
    /**
     * 构造函数，设置并启动 HTTP 服务器。
     * @param config 用于获取配置参数
     */
    explicit MetricsServer(const Core::Config& config);

    /**
     * 析构函数，停止 HTTP 服务器。
     */
    ~MetricsServer();

    /**
     * 获取 HTTP 服务器正在监听的地址。
     */
    std::string getListenAddress() const;

private:
    // 禁止复制
    MetricsServer(const MetricsServer&) = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;

    // HTTP 服务器线程的主函数
    void threadMain();

    // 处理新的 HTTP 连接
    void handleConnection(int clientFd);

    // 处理 HTTP 请求
    void handleRequest(int clientFd, const std::string& request);

    // 发送 HTTP 响应
    void sendResponse(int clientFd, int statusCode, const std::string& statusText,
                     const std::string& contentType, const std::string& body);

    // 是否应该退出
    std::atomic<bool> shouldExit;

    // 服务器套接字文件描述符
    int serverFd;
    
    // 监听地址
    std::string listenAddress;
    
    // 监听端口
    uint16_t port;

    // 服务器线程
    std::thread thread;

    // 条件变量和互斥锁，用于等待线程退出
    Core::ConditionVariable exitCond;
    Core::Mutex mutex;
};

} // namespace Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_METRICSSERVER_H
