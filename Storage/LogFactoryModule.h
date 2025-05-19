#ifndef LOGCABIN_STORAGE_LOGFACTORYMODULE_H
#define LOGCABIN_STORAGE_LOGFACTORYMODULE_H

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace google {
namespace protobuf {
class Message;
}
}

namespace LogCabin {
namespace Storage {

// Forward declaration
class Log;

namespace LogFactory {

/**
 * 抽象基类，代表一个可以创建Log对象的模块。
 */
class Module {
  public:
    explicit Module(const std::string& name) 
        : name(name) 
    {}
    
    virtual ~Module() {}
    
    /**
     * 创建一个Log对象的方法。
     * @param logPath 日志存储路径
     * @param configProto 配置参数
     * @return 创建的Log对象
     */
    virtual std::unique_ptr<Log> makeLog(
        const std::string& logPath,
        const google::protobuf::Message& configProto) = 0;
    
    /**
     * 获取模块名称。
     */
    std::string getName() const { return name; }
    
  private:
    std::string name;
};

/**
 * 日志工厂模块注册表，管理所有可用的Log模块。
 */
class ModuleRegistry {
  public:
    /**
     * 获取单例实例。
     */
    static ModuleRegistry& getInstance() {
        static ModuleRegistry instance;
        return instance;
    }
    
    /**
     * 注册一个模块。
     */
    void doRegister(const std::string& name, std::unique_ptr<Module> module) {
        std::lock_guard<std::mutex> lock(mutex);
        modules[name] = std::move(module);
    }
    
    /**
     * 获取一个已注册的模块。
     */
    Module* getModule(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = modules.find(name);
        if (it != modules.end())
            return it->second.get();
        return nullptr;
    }
    
    /**
     * 辅助类，用于在静态构造期间注册模块。
     */
    template<typename T>
    class Register {
      public:
        explicit Register(const std::string& name) {
            auto module = std::make_unique<T>(name);
            ModuleRegistry::getInstance().doRegister(name, std::move(module));
        }
    };
    
  private:
    ModuleRegistry() = default;
    ~ModuleRegistry() = default;
    
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;
    
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<Module>> modules;
};

} // namespace LogFactory
} // namespace Storage
} // namespace LogCabin

#endif // LOGCABIN_STORAGE_LOGFACTORYMODULE_H
