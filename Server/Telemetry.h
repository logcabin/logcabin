#ifndef LOGCABIN_SERVER_TELEMETRY_H
#define LOGCABIN_SERVER_TELEMETRY_H

#include <memory>
#include <string>
#include <unordered_map>

namespace LogCabin {
namespace Server {

/**
 * OpenTelemetry追踪集成类
 * 此类提供了分布式追踪功能，用于监控系统内部操作。
 * 依赖于OpenTelemetry C++ SDK，需要单独安装。
 */
class Telemetry {
  public:
    /**
     * 初始化追踪系统
     * @param serviceName 服务名称
     * @param instanceId 实例ID
     * @return 是否成功初始化
     */
    static bool init(const std::string& serviceName, 
                   const std::string& instanceId);

    /**
     * 创建一个新的追踪Span
     * @param spanName Span名称
     * @param parentSpanContext 父Span上下文(可选)
     * @return 新创建的Span ID
     */
    static std::string createSpan(const std::string& spanName, 
                               const std::string& parentSpanId = "");
    
    /**
     * 结束一个Span
     * @param spanId 要结束的Span ID
     */
    static void endSpan(const std::string& spanId);
    
    /**
     * 添加Span属性
     * @param spanId Span ID
     * @param key 属性键
     * @param value 属性值
     */
    static void addSpanAttribute(const std::string& spanId,
                              const std::string& key,
                              const std::string& value);
    
    /**
     * 记录Span事件
     * @param spanId Span ID
     * @param name 事件名称
     * @param attributes 事件属性
     */
    static void addSpanEvent(const std::string& spanId,
                          const std::string& name,
                          const std::unordered_map<std::string, std::string>& attributes = {});
    
    /**
     * 记录错误
     * @param spanId Span ID
     * @param message 错误信息
     */
    static void recordError(const std::string& spanId, 
                         const std::string& message);
    
    /**
     * 关闭追踪系统
     */
    static void shutdown();
    
    /**
     * 检查是否启用了追踪功能
     */
    static bool isEnabled();
};

} // namespace Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_TELEMETRY_H
