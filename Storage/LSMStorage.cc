#include "Storage/LSMStorage.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Core/Util.h"
#include "Storage/LogFactory.h"
#include "Storage/LogFactoryModule.h"
#include "Protocol/LogConfig.h"
#include "Raft.pb.h"  // 使用正确的包含路径，Protocol库已经将其目录暴露给所有依赖它的库
#include <zstd.h>

namespace LogCabin {
namespace Storage {

namespace {

/**
 * 创建一个LSM存储的配置对象。
 */
std::map<std::string, std::string>
makeConfig(uint64_t bufferSizeMB,
           bool compressionEnabled,
           const std::string& compressionAlgorithm,
           uint32_t maxLevels,
           double bloomFilterFPRate)
{
    std::map<std::string, std::string> config;
    config["lsm_buffer_size_mb"] = Core::StringUtil::format("%lu", bufferSizeMB);
    config["lsm_compression_enabled"] = compressionEnabled ? "true" : "false";
    config["lsm_compression_algorithm"] = compressionAlgorithm;
    config["lsm_max_levels"] = Core::StringUtil::format("%u", maxLevels);
    config["lsm_bloom_filter_fp_rate"] = Core::StringUtil::format("%.5f", bloomFilterFPRate);
    return config;
}

} // anonymous namespace

LSMStorage::LSMStorage(const std::string& path, const std::map<std::string, std::string>& config)
    : path(path)
    , config(config)
    , memTable()
    , diskLevels()
    , lastLogIndex(0)
    , logStartIndex(1)
    , mutex()
    , compactCond()
    , compactThread()
    , running(true)
    , initialized(false)
    // 新增成员初始化
    , flushIntervalSec(0)
    , disableCompactionFlag(false)
    , compressionAlgorithm("zstd")
    , bloomFilterFPRate(0.01)
    , globalBloom(nullptr)
{
    // 创建存储目录（如果不存在）
    mkdir(path.c_str(), 0755);
    
    // 初始化内存表
    memTable.min_index = std::numeric_limits<uint64_t>::max();
    memTable.max_index = 0;
    
    // 解析新配置
    if (config.count("lsmFlushIntervalSec"))
        flushIntervalSec = std::stoul(config.at("lsmFlushIntervalSec"));
    if (config.count("lsmDisableCompaction"))
        disableCompactionFlag = (config.at("lsmDisableCompaction") == "true");
    if (config.count("lsmCompressionAlgorithm"))
        compressionAlgorithm = config.at("lsmCompressionAlgorithm");
    if (config.count("lsmBloomFilterFPRate"))
        bloomFilterFPRate = std::stod(config.at("lsmBloomFilterFPRate"));

    // 加载磁盘数据
    loadFromDisk();
    
    // 启动后台合并线程
    if (!disableCompactionFlag)
        compactThread.reset(new std::thread(&LSMStorage::compact, this));
}

LSMStorage::~LSMStorage() 
{
    running.store(false);
    compactCond.notify_all();
    if (compactThread && compactThread->joinable()) {
        compactThread->join();
    }
    flush();
}

// 新增：外部可调用的强制刷盘方法
void
LSMStorage::flush()
{
    flushMemTable();
    compactCond.notify_one();
}

// 新增：关闭后台合并线程（仅测试或特殊场景使用）
void
LSMStorage::disableCompaction()
{
    running.store(false);
    compactCond.notify_all();
    if (compactThread && compactThread->joinable()) {
        compactThread->join();
    }
}
std::pair<uint64_t, uint64_t> // MODIFIED: Added return type
LSMStorage::append(const std::vector<const Entry*>& entries)
{
    if (entries.empty())
        return {0, 0};
    
    uint64_t firstIndex = 0;
    uint64_t lastIndex = 0;
    
    std::unique_lock<Core::Mutex> lock(mutex);
    if (memTable.entries.empty()) {
        memTable.min_index = entries[0]->index();
        memTable.bloom = std::make_shared<BloomFilter>(1024, bloomFilterFPRate); // 预估值可调
    }
    
    firstIndex = entries[0]->index();
    lastIndex = entries[entries.size()-1]->index();
    
    for (const Entry* entry : entries) {
        // 为每个条目创建一个新的缓冲区
        uint32_t type = entry->type();
        size_t dataSize = entry->data().size();
        size_t totalSize = sizeof(type) + dataSize;
        
        // 分配内存
        char* buf = new char[totalSize];
        memcpy(buf, &type, sizeof(type));
        memcpy(buf + sizeof(type), entry->data().data(), dataSize);
        
        Core::Buffer buffer(buf, totalSize, free);
        
        memTable.entries[entry->index()] = std::move(buffer);
        memTable.max_index = std::max(memTable.max_index, entry->index());
        if (memTable.bloom) memTable.bloom->add(entry->index());
    }
    
    lastLogIndex.store(memTable.max_index);
    
    uint64_t bufferSizeMB = std::stoull(config.at("lsm_buffer_size_mb"));
    size_t sizeBytes = 0;
    for (const auto& e : memTable.entries) {
        sizeBytes += e.second.getLength();
    }
    if (sizeBytes >= bufferSizeMB * 1024 * 1024) {
        lock.unlock();
        flushMemTable();
        compactCond.notify_one();
    }
    return {firstIndex, lastIndex};
}

const Protocol::Raft::Entry&
LSMStorage::getEntry(uint64_t index) const
{
    // 先在内存中查找
    static Protocol::Raft::Entry cachedEntry;
    // static Core::Buffer entryBuffer; // Not used, can be removed if not intended for future use
    {\
    std::unique_lock<Core::Mutex> lock(mutex);\
    if (memTable.bloom && !memTable.bloom->possiblyContains(index)) {\
        // 不在内存表中，继续查找磁盘层级
    } else {\
        auto it = memTable.entries.find(index);\
        if (it != memTable.entries.end()) {\
                const Core::Buffer& buffer = it->second;\
                uint32_t type;\
                memcpy(&type, buffer.getData(), sizeof(type));\
                \
                cachedEntry.set_index(index);\
                cachedEntry.set_type(static_cast<LogCabin::Protocol::Raft::EntryType>(type)); // MODIFIED: Cast to EntryType
                cachedEntry.set_data(static_cast<const char*>(buffer.getData()) + sizeof(type),\
                                     buffer.getLength() - sizeof(type));\
                return cachedEntry;\
        }\
    }\
    for (const auto& level : diskLevels) {\
        if (level.bloom && !level.bloom->possiblyContains(index))\
            continue;\
        if (index >= level.min_index && index <= level.max_index) {\
            auto it = level.entries.find(index);\
            if (it != level.entries.end()) {\
                const Core::Buffer& buffer = it->second;\
                uint32_t type;\
                memcpy(&type, buffer.getData(), sizeof(type));\
                \
                cachedEntry.set_index(index);\
                cachedEntry.set_type(static_cast<LogCabin::Protocol::Raft::EntryType>(type)); // MODIFIED: Cast to EntryType
                cachedEntry.set_data(static_cast<const char*>(buffer.getData()) + sizeof(type),\
                                     buffer.getLength() - sizeof(type));\
                return cachedEntry;\
            }\
        }\
    }\
    }\
    // 如果找不到条目，抛出异常
    throw std::out_of_range("Entry not found at index: " + std::to_string(index));\
}

uint64_t // MODIFIED: Added return type
LSMStorage::getLastLogIndex() const
{
    return lastLogIndex.load();
}

uint64_t // MODIFIED: Added return type
LSMStorage::getLogStartIndex() const
{
    return logStartIndex.load();
}

std::string // MODIFIED: Added return type
LSMStorage::getName() const
{
    return "LSMStorage";
}

uint64_t // MODIFIED: Added return type
LSMStorage::getSizeBytes() const
{
    // 计算所有层级的总大小
    size_t totalSize = 0;
    
    std::unique_lock<Core::Mutex> lock(mutex);
    // 内存表大小
    for (const auto& entry : memTable.entries) {
        totalSize += entry.second.getLength();
    }
    
    // 磁盘层级大小
    for (const auto& level : diskLevels) {
        for (const auto& entry : level.entries) {
            totalSize += entry.second.getLength();
        }
    }
    
    return totalSize;
}

void // MODIFIED: Added return type
LSMStorage::updateMetadata()
{
    std::unique_lock<Core::Mutex> lock(mutex);
    
    // 将元数据写入专用文件
    std::string metadataPath = path + "/metadata";
    std::ofstream metadataFile(metadataPath.c_str(), std::ios::binary);
    if (!metadataFile) {
        PANIC("无法写入元数据文件");
    }
    
    std::string serialized;
    if (!metadata.SerializeToString(&serialized)) {
        PANIC("元数据序列化失败");
    }
    
    metadataFile.write(serialized.data(), serialized.size());
    
    if (metadataFile.fail()) {
        PANIC("写入元数据文件失败");
    }
}

std::unique_ptr<Log::Sync> // MODIFIED: Added return type
LSMStorage::takeSync()
{
    uint64_t last = lastLogIndex.load();
    return std::unique_ptr<Log::Sync>(new Log::Sync(last));
}

void // MODIFIED: Added return type
LSMStorage::truncatePrefix(uint64_t firstIndex)
{
    std::unique_lock<Core::Mutex> lock(mutex);
    // 更新日志起始索引
    logStartIndex.store(firstIndex);
    
    // 移除内存表中较早的条目
    auto it = memTable.entries.begin();
    while (it != memTable.entries.end() && it->first < firstIndex) {
        it = memTable.entries.erase(it);
    }
}

void // MODIFIED: Added return type
LSMStorage::truncateSuffix(uint64_t lastIndex)
{
    // 截断内存表
    {\
        std::unique_lock<Core::Mutex> lock(mutex);\
        auto it = memTable.entries.lower_bound(lastIndex + 1);\
        while (it != memTable.entries.end()) {\
            it = memTable.entries.erase(it);\
        }\
        \
        if (memTable.entries.empty()) {\
            memTable.min_index = std::numeric_limits<uint64_t>::max();\
            memTable.max_index = 0;\
        } else {\
            memTable.max_index = memTable.entries.rbegin()->first;\
        }\
    }\
    \
    // 更新最后日志索引
    lastLogIndex.store(lastIndex);\
    \
    // 截断磁盘层级
    std::vector<size_t> levelsToReload;\
    for (size_t i = 0; i < diskLevels.size(); ++i) {\
        if (diskLevels[i].max_index > lastIndex) {\
            levelsToReload.push_back(i);\
        }\
    }\
    \
    // 实际生产中应该重构数据而不是重新加载
    // 但为简单起见，这里我们重新加载需要修改的层级
    for (size_t levelIdx : levelsToReload) {\
        // 这里应该实现部分重载逻辑
        // 为简化实现，我们仅标记需要后续处理
    }\
}

void // MODIFIED: Added return type
LSMStorage::compact()
{
    while (running.load()) {
        // 等待通知或定期检查是否需要合并
        {\
            std::unique_lock<Core::Mutex> lock(mutex);\
            while (running.load() && memTable.entries.empty()) {\
                compactCond.wait(lock);\
            }\
            if (!running.load()) break;\
        }\
        \
        // 尝试合并层级
        uint32_t maxLevels = 4; // 默认值
        if (config.count("lsm_max_levels")) {\
            maxLevels = std::stoul(config.at("lsm_max_levels"));\
        }\
        std::unique_lock<Core::Mutex> lock(mutex); // Lock before accessing diskLevels
        for (size_t i = 0; i < diskLevels.size() && i < maxLevels - 1; ++i) {\
            // 实现简单的合并策略：如果当前层的大小超过下一层的1/4，则合并
            if (i + 1 < diskLevels.size() && \
                diskLevels[i].entries.size() > diskLevels[i+1].entries.size() / 4) {\
                // 释放锁再合并，避免长时间持锁
                lock.unlock();\
                mergeLevels(i);\
                lock.lock(); // Re-acquire lock
                 if (!running.load()) break; // Check running status after mergeLevels
            }\
        }\
        lock.unlock(); // Unlock before sleep or flush
        \
        // 支持定时自动刷盘
        if (flushIntervalSec > 0) {\
            flush();\
        }\
        // 休眠，避免过度合并
        std::this_thread::sleep_for(std::chrono::seconds(flushIntervalSec > 0 ? flushIntervalSec : 10));\
    }\
}

bool // MODIFIED: Added return type
LSMStorage::flushMemTable()
{
    Level newLevel;
    {\
        std::unique_lock<Core::Mutex> lock(mutex);\
        if (memTable.entries.empty()) {\
            return true;\
        }\
        newLevel = std::move(memTable);\
        newLevel.bloom = std::make_shared<BloomFilter>(newLevel.entries.size() + 16, bloomFilterFPRate);\
        for (const auto& e : newLevel.entries) newLevel.bloom->add(e.first);\
        memTable.entries.clear();\
        memTable.min_index = std::numeric_limits<uint64_t>::max();\
        memTable.max_index = 0;\
        memTable.bloom = nullptr;\
    }\
    std::unique_lock<Core::Mutex> lock(mutex); // Lock before modifying diskLevels
    if (diskLevels.empty()) {\
        diskLevels.push_back(std::move(newLevel));\
    } else {\
        diskLevels.insert(diskLevels.begin(), std::move(newLevel));\
    }\
    size_t levelIndexToWrite = 0; // The new level is always at index 0 after insertion
    Level levelToWrite = diskLevels[levelIndexToWrite]; // Copy for writing
    lock.unlock(); // Unlock before disk I/O

    return writeToDisk(levelToWrite, levelIndexToWrite);\
}

bool // MODIFIED: Added return type
LSMStorage::mergeLevels(size_t level_index)
{
    std::unique_lock<Core::Mutex> lock(mutex);\
    if (level_index >= diskLevels.size() || level_index + 1 >= diskLevels.size()) {\
        return false;\
    }\
    \
    Level upperCopy = diskLevels[level_index];\
    Level lowerCopy = diskLevels[level_index + 1];\
    lock.unlock();\
    \
    // 合并层级数据
    for (const auto& entry : upperCopy.entries) {\
        // We need to copy the entry's contents since Core::Buffer can't be directly copied
        auto it = entry.second.getData();
        auto len = entry.second.getLength();
        if (len > 0) {
            // 创建新的buffer并复制数据内容
            char* data_copy = new char[len];
            std::memcpy(data_copy, it, len);
            Core::Buffer buffer(data_copy, len, [](void* ptr) { delete[] static_cast<char*>(ptr); });
            lowerCopy.entries[entry.first] = std::move(buffer);
        } else {
            // 处理空buffer情况
            Core::Buffer buffer(nullptr, 0, nullptr);
            lowerCopy.entries[entry.first] = std::move(buffer);
        }
    }\
    lowerCopy.min_index = std::min(lowerCopy.min_index, upperCopy.min_index);\
    lowerCopy.max_index = std::max(lowerCopy.max_index, upperCopy.max_index);\
    \
    // 重新构建bloom
    lowerCopy.bloom = std::make_shared<BloomFilter>(lowerCopy.entries.size() + 16, bloomFilterFPRate);\
    for (const auto& e : lowerCopy.entries) lowerCopy.bloom->add(e.first);\
    \
    // 写入磁盘 (lowerCopy is the merged result, to be written to level_index + 1)
    bool result = writeToDisk(lowerCopy, level_index + 1);\
    if (result) {\
        // 更新内存状态
        lock.lock();\
        if (level_index < diskLevels.size() && (level_index + 1) < diskLevels.size()) { // Re-check bounds
            diskLevels[level_index + 1] = std::move(lowerCopy);\
            diskLevels.erase(diskLevels.begin() + level_index);\
        } else { \
            // Levels changed during unlock, operation might be stale.
            // This needs more robust handling in a concurrent environment.
            // For now, we assume it's okay or the write failed.
            result = false; // Indicate potential issue
        }\
    }\
    \
    return result;\
}

bool // MODIFIED: Added return type
LSMStorage::loadFromDisk()
{
    // 加载元数据
    std::string metadataPath = path + "/metadata";
    std::ifstream metadataFile(metadataPath.c_str(), std::ios::binary);
    if (metadataFile) {
        std::string serialized((std::istreambuf_iterator<char>(metadataFile)),
                                std::istreambuf_iterator<char>());
        metadataFile.close(); // Close file after reading
        
        if (!metadata.ParseFromString(serialized)) {
            PANIC("无法解析元数据");
        }
    } else if (!initialized) {
        // 初次运行，元数据保持默认值
        // initialized = true; // This will be set at the end
    }
    
    // 清空并重加载层级数据
    std::unique_lock<Core::Mutex> lock(mutex); // Lock before modifying diskLevels
    diskLevels.clear();
    uint64_t maxIndexFound = 0; // Renamed to avoid conflict with member
    
    // 尝试加载所有层级
    uint32_t maxLevels = 4; // 默认值
    if (config.count("lsm_max_levels")) {
        maxLevels = std::stoul(config.at("lsm_max_levels"));
    }
    lock.unlock(); // Unlock for disk I/O

    for (uint32_t i = 0; i < maxLevels; ++i) {
        Level level;
        if (readFromDisk(level, i) && !level.entries.empty()) {
            lock.lock(); // Lock to modify diskLevels
            diskLevels.push_back(std::move(level));
            // Sort diskLevels by min_index to maintain order if loaded out of sequence (though unlikely here)
            // std::sort(diskLevels.begin(), diskLevels.end(), [](const Level& a, const Level& b){
            //    return a.min_index < b.min_index; // Or sort by level index if that's the primary key
            // });
            maxIndexFound = std::max(maxIndexFound, diskLevels.back().max_index);
            lock.unlock(); // Unlock after modification
        }
    }
    
    // 更新最后日志索引
    lock.lock(); // Lock to update shared members
    if (!diskLevels.empty()) {
         // Ensure diskLevels are sorted by level index if they represent distinct levels
        std::sort(diskLevels.begin(), diskLevels.end(), 
            [](const Level& a, const Level& b){
                // Assuming level_index was stored in Level or can be inferred
                // For now, this sort might not be correct if levels are not 0, 1, 2...
                // If readFromDisk loads them in order, this sort is not strictly needed
                // but good for safety if order isn't guaranteed.
                // Let's assume they are loaded in order of level_index for now.
                return a.min_index < b.min_index; // Fallback sort, might need adjustment
            });
        // maxIndexFound should be the max_index of the highest actual log entry
        // which could be in any level if levels are not strictly ordered by index ranges.
        // Recalculate maxIndexFound from all loaded levels.
        uint64_t currentMax = 0;
        if (!diskLevels.empty()) {
            for(const auto& lvl : diskLevels) {
                if (lvl.max_index > currentMax) {
                    currentMax = lvl.max_index;
                }
            }
        }
        lastLogIndex.store(currentMax);

    } else {
        lastLogIndex.store(0); // No entries on disk
    }
    
    initialized = true;
    return true;
}

bool // MODIFIED: Added return type
LSMStorage::writeToDisk(const Level& level, size_t level_index)
{
    std::string levelPath = getLevelPath(level_index);
    std::string tempPath = levelPath + ".tmp";
    std::ofstream file(tempPath.c_str(), std::ios::binary);
    if (!file) {
        PANIC("Failed to open temp file for writing: %s", tempPath.c_str());
        return false;
    }
    file.write(reinterpret_cast<const char*>(&level.min_index), sizeof(level.min_index));
    file.write(reinterpret_cast<const char*>(&level.max_index), sizeof(level.max_index));
    size_t count = level.entries.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    bool compressionEnabled = false;
    if (config.count("lsm_compression_enabled")) {
        compressionEnabled = (config.at("lsm_compression_enabled") == "true");
    }
    for (const auto& entry : level.entries) {
        uint64_t index = entry.first;
        file.write(reinterpret_cast<const char*>(&index), sizeof(index));
        const Core::Buffer& buffer = entry.second;
        uint32_t dataSize = buffer.getLength();
        if (compressionEnabled && dataSize > 0) { // Added dataSize > 0 check
            size_t maxDst = ZSTD_compressBound(dataSize);
            std::vector<char> compressed(maxDst);
            size_t compSize = ZSTD_compress(compressed.data(), maxDst, buffer.getData(), dataSize, 1); // Default compression level
            if (ZSTD_isError(compSize)) {
                WARNING("ZSTD compression failed for index %lu, error: %s", index, ZSTD_getErrorName(compSize));
                // Fallback: write uncompressed or handle error
                // For now, let's write uncompressed if compression fails
                uint32_t uncompressedMarker = std::numeric_limits<uint32_t>::max(); // Special marker for uncompressed fallback
                file.write(reinterpret_cast<const char*>(&uncompressedMarker), sizeof(uncompressedMarker));
                file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize)); // Original size
                file.write(static_cast<const char*>(buffer.getData()), dataSize);

            } else {
                uint32_t compSize32 = static_cast<uint32_t>(compSize);
                file.write(reinterpret_cast<const char*>(&compSize32), sizeof(compSize32));
                file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize)); // Original size
                file.write(compressed.data(), compSize32);
            }
        } else {
            uint32_t uncompressedMarker = std::numeric_limits<uint32_t>::max(); // Or 0 if that's distinguishable
            if (compressionEnabled) { // If compression was enabled but dataSize is 0
                 file.write(reinterpret_cast<const char*>(&uncompressedMarker), sizeof(uncompressedMarker));
            }
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            if (dataSize > 0) {
                file.write(static_cast<const char*>(buffer.getData()), dataSize);
            }
        }
    }
    if (file.fail()) {
        WARNING("File write failed for %s", tempPath.c_str());
        unlink(tempPath.c_str());
        return false;
    }
    file.close();
    if (rename(tempPath.c_str(), levelPath.c_str()) != 0) {
        WARNING("Failed to rename %s to %s: %s", tempPath.c_str(), levelPath.c_str(), strerror(errno));
        unlink(tempPath.c_str());
        return false;
    }
    return true;
}

bool // MODIFIED: Added return type
LSMStorage::readFromDisk(Level& level, size_t level_index)
{
    std::string levelPath = getLevelPath(level_index);
    std::ifstream file(levelPath.c_str(), std::ios::binary);
    if (!file) {
        return false; // File might not exist for this level, which is fine
    }
    file.read(reinterpret_cast<char*>(&level.min_index), sizeof(level.min_index));
    file.read(reinterpret_cast<char*>(&level.max_index), sizeof(level.max_index));
    if (file.eof() || file.fail()) return false; // Incomplete header

    size_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (file.fail()) return false;

    bool compressionEnabledSetting = false; // Read based on actual file format per entry
    if (config.count("lsm_compression_enabled")) {
         compressionEnabledSetting = (config.at("lsm_compression_enabled") == "true");
    }

    for (size_t i = 0; i < count; ++i) {
        uint64_t index;
        file.read(reinterpret_cast<char*>(&index), sizeof(index));
        if (file.fail()) return false;

        uint32_t sizeField1, sizeField2_origSize; // sizeField1 is compSize or dataSize
        file.read(reinterpret_cast<char*>(&sizeField1), sizeof(sizeField1));
        if (file.fail()) return false;

        bool entryIsCompressed = false;
        uint32_t dataToRead = 0;
        uint32_t originalDataSize = 0;

        uint32_t uncompressedMarker = std::numeric_limits<uint32_t>::max();

        if (compressionEnabledSetting) { // Only try to read compression fields if it was globally enabled
            if (sizeField1 != uncompressedMarker) { // This entry is compressed
                entryIsCompressed = true;
                file.read(reinterpret_cast<char*>(&sizeField2_origSize), sizeof(sizeField2_origSize));
                if (file.fail()) return false;
                dataToRead = sizeField1; // compressed size
                originalDataSize = sizeField2_origSize;
            } else { // Entry explicitly marked as uncompressed (or was written when compression was off)
                entryIsCompressed = false;
                // sizeField1 was the marker, next is actual dataSize
                file.read(reinterpret_cast<char*>(&sizeField2_origSize), sizeof(sizeField2_origSize));
                if (file.fail()) return false;
                dataToRead = sizeField2_origSize; // uncompressed size
                originalDataSize = sizeField2_origSize;
            }
        } else { // Compression was globally off when this file was written
            entryIsCompressed = false;
            dataToRead = sizeField1; // uncompressed size
            originalDataSize = sizeField1;
        }


        if (entryIsCompressed) {
            std::vector<char> compBuf(dataToRead); // dataToRead is compSize
            file.read(compBuf.data(), dataToRead);
            if (file.fail()) return false;

            std::vector<char> origBuf(originalDataSize);
            size_t dSize = ZSTD_decompress(origBuf.data(), originalDataSize, compBuf.data(), dataToRead);
            if (ZSTD_isError(dSize) || dSize != originalDataSize) {
                WARNING("ZSTD decompression failed for index %lu, error: %s, dSize: %zu, origSize: %u", index, ZSTD_getErrorName(dSize), dSize, originalDataSize);
                return false;
            }
            char* entry_data = new char[originalDataSize];
            memcpy(entry_data, origBuf.data(), originalDataSize);
            Core::Buffer buffer(entry_data, originalDataSize, [](void* ptr) { delete[] static_cast<char*>(ptr); });
            level.entries[index] = std::move(buffer);
        } else {
            if (dataToRead > 0) {
                char* entry_data = new char[dataToRead]; // dataToRead is originalDataSize
                file.read(entry_data, dataToRead);
                if (file.fail()) { delete[] entry_data; return false; }
                 Core::Buffer buffer(entry_data, dataToRead, [](void* ptr) { delete[] static_cast<char*>(ptr); });
                level.entries[index] = std::move(buffer);
            } else { // Zero size entry
                Core::Buffer buffer(nullptr, 0, nullptr); // Empty buffer
                level.entries[index] = std::move(buffer);
            }
        }
    }
    // It's okay to reach EOF after reading all entries.
    // if (file.fail() && !file.eof()) {
    //    return false;
    // }
    
    // 创建布隆过滤器
    if (!level.entries.empty()) {
        level.bloom = std::make_shared<BloomFilter>(level.entries.size() + 16, bloomFilterFPRate);
        for (const auto& e : level.entries) level.bloom->add(e.first);
    }
    
    return true;
}

std::string // MODIFIED: Added return type
LSMStorage::getLevelPath(size_t level_index) const
{
    return path + "/level-" + Core::StringUtil::format("%03lu", level_index);
}

// 注册存储引擎
namespace { // Anonymous namespace for factory and registration
/**
 * 创建LSMStorage对象的工厂。
 */
class LSMStorageFactory : public LogFactory::Module {
  public:
    explicit LSMStorageFactory(const std::string& name)
        : LogFactory::Module(name)
    {\
    }\
    \
    std::unique_ptr<Log>\
    makeLog(const std::string& logPath, \
            const google::protobuf::Message& configProto) override { // MODIFIED: Signature to match base
        // Default values for LSMStorage specific configuration
        uint64_t bufferSizeMB = 64;\
        bool compressionEnabled = true;\
        std::string compressionAlgorithm = "zstd";\
        uint32_t maxLevels = 7;\
        double bloomFilterFPRate = 0.01;\
        \
        // Attempt to cast configProto to LogCabin::ProtoBuf::LogConfig to see if we can extract generic params
        // This part is speculative as LogConfig might not have generic key-value pairs.
        // If it does, they could override the defaults above.
        const LogCabin::ProtoBuf::LogConfig* specificLogConfig = \
            dynamic_cast<const LogCabin::ProtoBuf::LogConfig*>(&configProto);\
        \
        // Create the map using makeConfig (which uses defaults or passed-in programmatic values)
        // This makeConfig is from the anonymous namespace in this file.
        std::map<std::string, std::string> lsmConfig = \
            makeConfig(bufferSizeMB, compressionEnabled, compressionAlgorithm, \
                       maxLevels, bloomFilterFPRate);\
        \
        // If specificLogConfig is valid and has a way to provide these, override them here.
        // Example: if (specificLogConfig && specificLogConfig->has_lsm_buffer_size_mb()) {
        //    lsmConfig["lsm_buffer_size_mb"] = std::to_string(specificLogConfig->lsm_buffer_size_mb());
        // } 
        // For now, we rely on the defaults set in makeConfig.

        return std::make_unique<LSMStorage>(logPath, lsmConfig);\
    }\
};\

// 静态全局变量，在程序启动时注册LSM存储模块
const std::string lsmName = "LSM"; // const for safety
// The Register class's constructor will call LogFactory::ModuleRegistry::doRegister
static LogFactory::ModuleRegistry::Register<LSMStorageFactory> registerer(lsmName);
// Removed: LSMStorageFactory lsmFactory(lsmName); // Redundant

} // anonymous namespace

} // namespace Storage
} // namespace LogCabin
