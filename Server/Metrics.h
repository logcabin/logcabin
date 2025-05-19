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

#ifndef LOGCABIN_SERVER_METRICS_H
#define LOGCABIN_SERVER_METRICS_H

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Mutex.h"

namespace LogCabin {
namespace Server {

/**
 * 度量标准管理系统，提供 Prometheus 兼容的指标收集和导出。
 * 这个类管理各种类型的指标（计数器、仪表盘、直方图、总结），
 * 并能够输出为 Prometheus 格式。
 */
class Metrics {
public:
    /**
     * 度量标准类型
     */
    enum class Type {
        COUNTER,   // 只增加的计数器
        GAUGE,     // 可以上下变化的仪表盘
        HISTOGRAM, // 观察值的直方图
        SUMMARY    // 分位数统计
    };

    /**
     * 度量标准基类
     */
    class Metric {
    public:
        Metric(const std::string& name, 
               const std::string& help,
               Type type,
               const std::map<std::string, std::string>& labels = {});
        virtual ~Metric() = default;
        
        /**
         * 获取 Prometheus 格式的指标定义
         */
        std::string getDefinition() const;
        
        /**
         * 获取 Prometheus 格式的当前指标值
         */
        virtual std::string getValue() const = 0;

        /**
         * 获取 Prometheus 格式的完整输出，包含定义和值
         */
        std::string getOutput() const;

        /**
         * 获取指标名称
         */
        const std::string& getName() const { return name; }

        /**
         * 获取指标标签
         */
        const std::map<std::string, std::string>& getLabels() const { return labels; }

    protected:
        // 指标名称
        std::string name;
        // 指标帮助信息
        std::string help;
        // 指标类型
        Type type;
        // 指标标签
        std::map<std::string, std::string> labels;
    };

    /**
     * 计数器：只能增加的指标
     */
    class Counter : public Metric {
    public:
        Counter(const std::string& name, 
                const std::string& help,
                const std::map<std::string, std::string>& labels = {});
        
        /**
         * 增加计数器值
         */
        void increment(double value = 1.0);
        
        /**
         * 获取当前值
         */
        double get() const;
        
        /**
         * 获取 Prometheus 格式的当前值
         */
        std::string getValue() const override;
        
    private:
        // 计数器值
        std::atomic<double> value;
    };

    /**
     * 仪表盘：可以上下变化的指标
     */
    class Gauge : public Metric {
    public:
        Gauge(const std::string& name, 
              const std::string& help,
              const std::map<std::string, std::string>& labels = {});
        
        /**
         * 设置仪表盘值
         */
        void set(double value);
        
        /**
         * 增加仪表盘值
         */
        void increment(double value = 1.0);
        
        /**
         * 减少仪表盘值
         */
        void decrement(double value = 1.0);
        
        /**
         * 获取当前值
         */
        double get() const;
        
        /**
         * 获取 Prometheus 格式的当前值
         */
        std::string getValue() const override;
        
    private:
        // 仪表盘值
        std::atomic<double> value;
    };

    /**
     * 直方图：观察值的分布
     */
    class Histogram : public Metric {
    public:
        Histogram(const std::string& name, 
                  const std::string& help,
                  const std::vector<double>& buckets = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10},
                  const std::map<std::string, std::string>& labels = {});
        
        /**
         * 观察一个值
         */
        void observe(double value);
        
        /**
         * 获取 Prometheus 格式的当前值
         */
        std::string getValue() const override;
        
    private:
        // 存储桶边界
        std::vector<double> buckets;
        // 每个存储桶的计数
        mutable std::vector<std::atomic<uint64_t>> bucketCounts;
        // 总和
        mutable std::atomic<double> sum;
        // 计数
        mutable std::atomic<uint64_t> count;
        // 用于保护桶更新的互斥锁
        mutable Core::Mutex mutex;
    };

    /**
     * 单例访问器
     */
    static Metrics& getInstance();

    /**
     * 注册一个计数器
     */
    std::shared_ptr<Counter> createCounter(
        const std::string& name,
        const std::string& help,
        const std::map<std::string, std::string>& labels = {});

    /**
     * 注册一个仪表盘
     */
    std::shared_ptr<Gauge> createGauge(
        const std::string& name,
        const std::string& help,
        const std::map<std::string, std::string>& labels = {});

    /**
     * 注册一个直方图
     */
    std::shared_ptr<Histogram> createHistogram(
        const std::string& name,
        const std::string& help,
        const std::vector<double>& buckets = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10},
        const std::map<std::string, std::string>& labels = {});

    /**
     * 获取所有指标的 Prometheus 格式输出
     */
    std::string getMetricsOutput() const;

private:
    // 私有构造函数（单例模式）
    Metrics();
    
    // 用于保护指标注册的互斥锁
    mutable Core::Mutex mutex;
    
    // 存储所有指标
    std::vector<std::shared_ptr<Metric>> metrics;
    
    // 通过名称和标签获取或创建指标
    template<typename T, typename... Args>
    std::shared_ptr<T> getOrCreateMetric(
        const std::string& name,
        const std::map<std::string, std::string>& labels,
        Args&&... args);
};

} // namespace Server
} // namespace LogCabin

#endif // LOGCABIN_SERVER_METRICS_H
