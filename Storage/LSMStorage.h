#ifndef LOGCABIN_STORAGE_LSMSTORAGE_H
#define LOGCABIN_STORAGE_LSMSTORAGE_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>    // 添加thread头文件
#include <vector>
#include <functional>

#include "Core/Buffer.h"
#include "Core/ConditionVariable.h"
#include "Core/Mutex.h"
#include "Storage/Log.h"
#include "Storage/bloom_filter.hpp"

namespace LogCabin {
namespace Storage {

/**
 * 基于LSM树实现的日志存储系统。
 * 
 * LSM (Log-Structured Merge)树是一种针对写入优化的数据结构，类似于LevelDB或RocksDB的底层结构。
 * 它将写入先放入内存中的有序表，然后周期性地将这些表合并并写入磁盘。这种方式减少了随机写入，
 * 提供了更高的写入吞吐量，适合LogCabin这类以写入为主的系统。
 */
class LSMStorage : public Log {
  public:
    /**
     * 构造函数。
     * @param path 存储路径
     * @param config 存储配置对象
     */
    LSMStorage(const std::string& path, const std::map<std::string, std::string>& config);
    
    /**
     * 析构函数。
     * 确保后台合并线程安全退出，并释放所有资源。
     */
    virtual ~LSMStorage();
    
    /**
     * 附加一个条目到日志中。参见Log::append接口。
     */
    virtual std::pair<uint64_t, uint64_t> append(const std::vector<const Entry*>& entries);
    
    /**
     * 读取指定位置的条目。参见Log::getEntry接口。
     */
    virtual const Entry& getEntry(uint64_t index) const;
    
    /**
     * 获取最后一个条目的索引。参见Log::getLastLogIndex接口。
     */
    virtual uint64_t getLastLogIndex() const;
    
    /**
     * 获取日志起始索引
     */
    virtual uint64_t getLogStartIndex() const;
    
    /**
     * 获取名称
     */
    virtual std::string getName() const;
    
    /**
     * 获取大小（字节）
     */
    virtual uint64_t getSizeBytes() const;
    
    /**
     * 获取同步对象
     */
    virtual std::unique_ptr<Sync> takeSync();
    
    /**
     * 从头部截断日志
     */
    virtual void truncatePrefix(uint64_t firstIndex);
    
    /**
     * 从尾部截断日志
     */
    virtual void truncateSuffix(uint64_t lastIndex);
    
    /**
     * 更新元数据
     */
    virtual void updateMetadata();
    
    /**
     * 强制刷盘，将内存表数据立即写入磁盘。
     */
    void flush();
    
    /**
     * 关闭后台合并线程，仅用于测试或特殊场景。
     */
    void disableCompaction();
    
  private:
    /**
     * 后台线程入口，执行LSM树的合并操作。
     */
    void compact();
    
    /**
     * 布隆过滤器类型声明（具体实现可用第三方库，如libbloom/boost等）
     */
    struct BloomFilter {
        bloom_filter filter;
        BloomFilter(size_t expected_entries, double fp_rate) {
            bloom_parameters params;
            params.projected_element_count = expected_entries;
            params.false_positive_probability = fp_rate;
            params.compute_optimal_parameters();
            filter = bloom_filter(params);
        }
        void add(uint64_t key) { filter.insert(reinterpret_cast<const unsigned char*>(&key), sizeof(key)); }
        bool possiblyContains(uint64_t key) const { return filter.contains(reinterpret_cast<const unsigned char*>(&key), sizeof(key)); }
    };
    using BloomFilterPtr = std::shared_ptr<BloomFilter>;

    /**
     * 表示LSM树中一个层级的数据。
     */
    struct Level {
        std::map<uint64_t, Core::Buffer> entries;
        uint64_t min_index;
        uint64_t max_index;
        BloomFilterPtr bloom; // 新增
    };
    
    /**
     * 将内存中的数据刷新到磁盘。
     */
    bool flushMemTable();
    
    /**
     * 合并两个层级。
     */
    bool mergeLevels(size_t level_index);
    
    /**
     * 从磁盘加载现有数据。
     */
    bool loadFromDisk();
    
    /**
     * 写入磁盘操作。
     */
    bool writeToDisk(const Level& level, size_t level_index);
    
    /**
     * 从磁盘读取指定层级。
     */
    bool readFromDisk(Level& level, size_t level_index);
    
    /**
     * 获取层级文件路径。
     */
    std::string getLevelPath(size_t level_index) const;
    
    // 存储路径
    const std::string path;
    
    // 配置信息
    const std::map<std::string, std::string> config;
    
    // 内存表，尚未写入磁盘的数据
    Level memTable;
    
    // 磁盘上的各级数据
    std::vector<Level> diskLevels;
    
    // 最后一个日志索引
    std::atomic<uint64_t> lastLogIndex;
    
    // 日志开始索引
    std::atomic<uint64_t> logStartIndex;
    
    // 用于保护内存表的互斥锁
    mutable Core::Mutex mutex;
    
    // 条件变量，用于通知后台合并线程
    Core::ConditionVariable compactCond;
    
    // 后台合并线程
    std::unique_ptr<std::thread> compactThread;
    
    // 线程是否应继续运行
    std::atomic<bool> running;
    
    // 磁盘是否已经初始化
    bool initialized;

    // 刷盘间隔（秒），0为仅按需刷盘
    uint32_t flushIntervalSec = 0;
    // 是否禁用后台合并
    bool disableCompactionFlag = false;
    // 压缩算法
    std::string compressionAlgorithm;
    // 布隆过滤器误报率
    double bloomFilterFPRate = 0.01;

    // 预留：全局布隆过滤器（可选）
    BloomFilterPtr globalBloom;
};

} // namespace Storage
} // namespace LogCabin

#endif // LOGCABIN_STORAGE_LSMSTORAGE_H
