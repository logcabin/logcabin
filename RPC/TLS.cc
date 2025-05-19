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

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <string>
#include <stdexcept>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "RPC/TLS.h"

namespace LogCabin {
namespace RPC {
namespace TLS {

// 初始化 OpenSSL
void 
initialize()
{
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    if (!RAND_poll()) {
        throw std::runtime_error("Failed to initialize OpenSSL RAND");
    }
}

// 清理 OpenSSL
void 
cleanup()
{
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();
    EVP_cleanup();
}

// 打印 OpenSSL 错误信息
static std::string 
getOpenSSLErrors()
{
    std::string errors;
    unsigned long err;
    char errBuf[256];
    
    while ((err = ERR_get_error()) != 0) {
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        if (!errors.empty()) {
            errors += "; ";
        }
        errors += errBuf;
    }
    
    return errors;
}

Context::Context(SecurityLevel securityLevel,
                 const std::string& certPath,
                 const std::string& keyPath,
                 const std::string& caPath)
    : verifyCallback()
    , securityLevel(securityLevel)
    , certPath(certPath)
    , keyPath(keyPath)
    , caPath(caPath)
    , sslContext(nullptr)
{
    initializeSSL();
    loadCertificates();
}

Context::~Context()
{
    if (sslContext)
        SSL_CTX_free(sslContext);
}

SSL_CTX* 
Context::getSSLContext() const
{
    return sslContext;
}

void 
Context::setVerifyCallback(VerifyCallback callback)
{
    verifyCallback = callback;
    
    // 设置 OpenSSL 验证回调
    if (sslContext && verifyCallback) {
        SSL_CTX_set_verify(sslContext, SSL_VERIFY_PEER, [](int ok, X509_STORE_CTX* ctx) -> int {
            SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
            if (!ssl)
                return 0;
                
            X509* cert = X509_STORE_CTX_get_current_cert(ctx);
            if (!cert)
                return 0;
                
            Context* context = static_cast<Context*>(SSL_get_ex_data(ssl, 0));
            if (!context || !context->verifyCallback)
                return 0;
                
            // 获取主题名
            char subjectNameBuf[256];
            X509_NAME_oneline(X509_get_subject_name(cert), subjectNameBuf, sizeof(subjectNameBuf));
            std::string subjectName(subjectNameBuf);
            
            return context->verifyCallback(cert, subjectName) ? 1 : 0;
        });
    }
}

void 
Context::initializeSSL()
{
    // 创建 TLS 上下文
    const SSL_METHOD* method = TLS_method();
    if (!method) {
        throw std::runtime_error(
            Core::StringUtil::format("Failed to create TLS method: %s",
                                     getOpenSSLErrors().c_str()));
    }
    
    sslContext = SSL_CTX_new(method);
    if (!sslContext) {
        throw std::runtime_error(
            Core::StringUtil::format("Failed to create SSL context: %s",
                                     getOpenSSLErrors().c_str()));
    }
    
    // 设置最小 TLS 版本为 1.2
    if (!SSL_CTX_set_min_proto_version(sslContext, TLS1_2_VERSION)) {
        throw std::runtime_error(
            Core::StringUtil::format("Failed to set minimum TLS version: %s",
                                     getOpenSSLErrors().c_str()));
    }

    // 设置安全的密码套件
    if (SSL_CTX_set_cipher_list(sslContext, "HIGH:!aNULL:!MD5:!RC4") != 1) {
        throw std::runtime_error(
            Core::StringUtil::format("Failed to set cipher list: %s",
                                     getOpenSSLErrors().c_str()));
    }
}

void 
Context::loadCertificates()
{
    if (securityLevel == SecurityLevel::ALLOW_INSECURE) {
        WARNING("Running with TLS security level set to ALLOW_INSECURE. "
                "This is not recommended for production use.");
        return;
    }
    
    // 加载证书和私钥
    if (!certPath.empty() && !keyPath.empty()) {
        if (SSL_CTX_use_certificate_file(sslContext, certPath.c_str(), 
                                       SSL_FILETYPE_PEM) != 1) {
            throw std::runtime_error(
                Core::StringUtil::format("Failed to load certificate %s: %s",
                                       certPath.c_str(), 
                                       getOpenSSLErrors().c_str()));
        }
        
        if (SSL_CTX_use_PrivateKey_file(sslContext, keyPath.c_str(), 
                                      SSL_FILETYPE_PEM) != 1) {
            throw std::runtime_error(
                Core::StringUtil::format("Failed to load private key %s: %s",
                                       keyPath.c_str(), 
                                       getOpenSSLErrors().c_str()));
        }
        
        if (SSL_CTX_check_private_key(sslContext) != 1) {
            throw std::runtime_error(
                Core::StringUtil::format("Certificate and private key do not match: %s",
                                       getOpenSSLErrors().c_str()));
        }
    } else if (securityLevel == SecurityLevel::REQUIRE_SECURE) {
        throw std::runtime_error(
            "Security level is REQUIRE_SECURE but certificate or key path is not provided");
    }
    
    // 加载 CA 证书
    if (!caPath.empty()) {
        if (SSL_CTX_load_verify_locations(sslContext, caPath.c_str(), nullptr) != 1) {
            throw std::runtime_error(
                Core::StringUtil::format("Failed to load CA certificates %s: %s",
                                       caPath.c_str(), 
                                       getOpenSSLErrors().c_str()));
        }
    } else if (securityLevel == SecurityLevel::REQUIRE_SECURE) {
        WARNING("CA path is not provided. Client certificate verification will not work.");
    }
}

} // namespace TLS
} // namespace RPC
} // namespace LogCabin
