#ifndef LOGCABIN_SERVER_WEBADMIN_SERVER_H
#define LOGCABIN_SERVER_WEBADMIN_SERVER_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "Core/Config.h"
#include "Core/Mutex.h"

namespace LogCabin {
namespace Server {
namespace WebAdmin {

/**
 * Web管理界面服务器
 * 提供基于HTTP的管理界面，用于监控和控制LogCabin集群
 */
class Server {
  public:
    /**
     * 构造Web管理服务器
     * @param config 配置对象
     */
    explicit Server(const Core::Config& config);

    /**
     * 析构函数
     */
    ~Server();

    /**
     * 启动Web管理服务器
     */
    void start();

    /**
     * 停止Web管理服务器
     */
    void stop();

    /**
     * 检查服务器是否正在运行
     */
    bool isRunning() const;

  private:
    /**
     * 服务器主循环
     */
    void serverLoop();

    /**
     * 处理HTTP请求
     */
    void handleRequest();

    // 监听地址
    std::string listenAddress;
    
    // 监听端口
    uint16_t port;
    
    // 是否启用HTTPS
    bool httpsEnabled;
    
    // 证书文件路径
    std::string certFile;
    
    // 密钥文件路径
    std::string keyFile;
    
    // 是否需要认证
    bool authEnabled;
    
    // 认证类型: basic, digest, token, oauth
    std::string authType;
    
    // 允许访问的IP列表
    std::vector<std::string> allowedIPs;

    // 运行状态
    std::atomic<bool> running;
    
    // 服务器线程
    std::unique_ptr<std::thread> serverThread;

    // Web服务器实例
    void* webServer;

    // 保护状态的互斥锁
    mutable Core::Mutex mutex;
};

} // namespace WebAdmin
} // namespace Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_WEBADMIN_SERVER_H
