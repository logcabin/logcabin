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

#include <memory>
#include <string>
#include <functional>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifndef LOGCABIN_RPC_TLS_H
#define LOGCABIN_RPC_TLS_H

namespace LogCabin {
namespace RPC {
namespace TLS {

/**
 * TLS 安全级别配置
 */
enum class SecurityLevel {
    // 允许所有连接，即使是不安全的
    ALLOW_INSECURE,
    // 允许使用 TLS 但不强制执行
    PREFER_SECURE,
    // 要求使用 TLS，否则拒绝连接
    REQUIRE_SECURE
};

/**
 * SSL/TLS 上下文管理器
 */
class Context {
public:
    /**
     * 创建新的 TLS 上下文
     * @param securityLevel 安全级别要求
     * @param certPath 证书路径
     * @param keyPath 私钥路径
     * @param caPath CA证书路径
     */
    Context(SecurityLevel securityLevel,
            const std::string& certPath = "",
            const std::string& keyPath = "",
            const std::string& caPath = "");

    /**
     * 析构函数
     */
    ~Context();

    /**
     * 获取 OpenSSL SSL_CTX 对象
     */
    SSL_CTX* getSSLContext() const;

    /**
     * 验证客户端证书回调函数类型
     */
    using VerifyCallback = std::function<bool(X509*, const std::string&)>;
    
    /**
     * 设置验证回调函数
     */
    void setVerifyCallback(VerifyCallback callback);

private:
    // 初始化 SSL 上下文
    void initializeSSL();
    // 加载证书和密钥
    void loadCertificates();
    // 验证回调函数
    VerifyCallback verifyCallback;
    // 安全级别
    SecurityLevel securityLevel;
    // 证书路径
    std::string certPath;
    // 私钥路径
    std::string keyPath;
    // CA 证书路径
    std::string caPath;
    // OpenSSL 上下文
    SSL_CTX* sslContext;
};

/**
 * 初始化 OpenSSL 库
 * 在应用启动时调用一次
 */
void initialize();

/**
 * 清理 OpenSSL 库
 * 在应用退出时调用一次
 */
void cleanup();

} // namespace TLS
} // namespace RPC
} // namespace LogCabin

#endif // LOGCABIN_RPC_TLS_H
