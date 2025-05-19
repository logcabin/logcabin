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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Server/Metrics.h"
#include "Server/MetricsServer.h"

namespace LogCabin {
namespace Server {

using Core::StringUtil::format;

MetricsServer::MetricsServer(const Core::Config& config)
    : shouldExit(false)
    , serverFd(-1)
    , listenAddress(config.read<std::string>("metricsListenAddress", "127.0.0.1"))
    , port(config.read<uint16_t>("metricsPort", 9090))
    , thread()
    , exitCond()
    , mutex()
{
    // 创建套接字
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        PANIC("Failed to create server socket: %s", strerror(errno));
    }

    // 设置地址重用选项
    int optval = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR,
                  &optval, sizeof(optval)) < 0) {
        close(serverFd);
        PANIC("Failed to set socket options: %s", strerror(errno));
    }

    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, listenAddress.c_str(), &addr.sin_addr) <= 0) {
        close(serverFd);
        PANIC("Invalid address: %s", listenAddress.c_str());
    }

    if (bind(serverFd, reinterpret_cast<struct sockaddr*>(&addr),
            sizeof(addr)) < 0) {
        close(serverFd);
        PANIC("Failed to bind to %s:%u: %s",
              listenAddress.c_str(), port, strerror(errno));
    }

    // 开始监听
    if (listen(serverFd, 5) < 0) {
        close(serverFd);
        PANIC("Failed to listen on socket: %s", strerror(errno));
    }

    // 启动线程
    thread = std::thread(&MetricsServer::threadMain, this);
    
    NOTICE("Metrics server listening on %s:%u", listenAddress.c_str(), port);
}

MetricsServer::~MetricsServer()
{
    shouldExit.store(true);
    if (serverFd >= 0) {
        shutdown(serverFd, SHUT_RDWR);
        close(serverFd);
        serverFd = -1;
    }
    
    if (thread.joinable()) {
        thread.join();
    }
}

std::string
MetricsServer::getListenAddress() const
{
    return format("%s:%u", listenAddress.c_str(), port);
}

void
MetricsServer::threadMain()
{
    while (!shouldExit.load()) {
        // 设置超时，避免一直阻塞，防止无法及时退出
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverFd, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int result = select(serverFd + 1, &readfds, NULL, NULL, &tv);
        if (result < 0) {
            if (errno != EINTR) {
                ERROR("Error in select(): %s", strerror(errno));
            }
            continue;
        }
        
        if (result == 0) {
            // 超时，继续循环
            continue;
        }
        
        // 有新的连接
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientFd = accept(serverFd,
                             reinterpret_cast<struct sockaddr*>(&clientAddr),
                             &clientAddrLen);
        if (clientFd < 0) {
            ERROR("Failed to accept connection: %s", strerror(errno));
            continue;
        }

        // 处理连接
        handleConnection(clientFd);
        close(clientFd);
    }
}

void
MetricsServer::handleConnection(int clientFd)
{
    // 读取请求
    std::string request;
    char buffer[4096];
    bool requestComplete = false;
    
    while (!requestComplete) {
        ssize_t bytesRead = read(clientFd, buffer, sizeof(buffer) - 1);
        if (bytesRead < 0) {
            ERROR("Error reading from socket: %s", strerror(errno));
            return;
        } else if (bytesRead == 0) {
            // 连接关闭
            break;
        }
        
        buffer[bytesRead] = '\0';
        request.append(buffer);
        
        // 检查是否接收到完整的 HTTP 请求
        if (request.find("\r\n\r\n") != std::string::npos) {
            requestComplete = true;
        }
    }
    
    // 处理请求
    handleRequest(clientFd, request);
}

void
MetricsServer::handleRequest(int clientFd, const std::string& request)
{
    // 解析请求第一行
    std::istringstream requestStream(request);
    std::string method, path, httpVersion;
    requestStream >> method >> path >> httpVersion;
    
    if (method != "GET") {
        // 只支持 GET 请求
        sendResponse(clientFd, 405, "Method Not Allowed",
                    "text/plain", "Only GET requests are supported");
        return;
    }
    
    if (path == "/metrics") {
        // 提供 Prometheus 格式的指标
        std::string metricsOutput = Metrics::getInstance().getMetricsOutput();
        sendResponse(clientFd, 200, "OK", "text/plain", metricsOutput);
    } else if (path == "/" || path == "/index.html") {
        // 提供简单的 HTML 页面
        std::string html =
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "  <title>LogCabin Metrics</title>\n"
            "</head>\n"
            "<body>\n"
            "  <h1>LogCabin Metrics</h1>\n"
            "  <p>Prometheus metrics are available at <a href=\"/metrics\">/metrics</a></p>\n"
            "</body>\n"
            "</html>\n";
        sendResponse(clientFd, 200, "OK", "text/html", html);
    } else {
        // 404 Not Found
        sendResponse(clientFd, 404, "Not Found",
                    "text/plain", "The requested resource was not found");
    }
}

void
MetricsServer::sendResponse(int clientFd, int statusCode, const std::string& statusText,
                          const std::string& contentType, const std::string& body)
{
    std::string response = format("HTTP/1.1 %d %s\r\n"
                                 "Content-Type: %s\r\n"
                                 "Content-Length: %zu\r\n"
                                 "Connection: close\r\n"
                                 "\r\n"
                                 "%s",
                                 statusCode, statusText.c_str(),
                                 contentType.c_str(),
                                 body.size(),
                                 body.c_str());
    
    ssize_t bytesWritten = write(clientFd, response.c_str(), response.size());
    if (bytesWritten < 0 || static_cast<size_t>(bytesWritten) < response.size()) {
        ERROR("Failed to write HTTP response: %s", strerror(errno));
    }
}

} // namespace Server
} // namespace LogCabin
