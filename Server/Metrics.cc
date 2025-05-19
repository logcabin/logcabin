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

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#include "Core/StringUtil.h"
#include "Server/Metrics.h"

namespace LogCabin {
namespace Server {

// 所有 format 函数调用应使用全限定名称 Core::StringUtil::format

//------------------------------------------------------------------------------
// Metrics::Metric Implementation
//------------------------------------------------------------------------------

Metrics::Metric::Metric(const std::string& name,
                         const std::string& help,
                         Type type,
                         const std::map<std::string, std::string>& labels)
    : name(name)
    , help(help)
    , type(type)
    , labels(labels)
{
}

std::string
Metrics::Metric::getDefinition() const
{
    std::ostringstream oss;
    oss << "# HELP " << name << " " << help << "\n";
    
    const char* typeStr = nullptr;
    switch (type) {
        case Type::COUNTER:
            typeStr = "counter";
            break;
        case Type::GAUGE:
            typeStr = "gauge";
            break;
        case Type::HISTOGRAM:
            typeStr = "histogram";
            break;
        case Type::SUMMARY:
            typeStr = "summary";
            break;
    }
    
    oss << "# TYPE " << name << " " << typeStr << "\n";
    return oss.str();
}

std::string
Metrics::Metric::getOutput() const
{
    return getDefinition() + getValue();
}

//------------------------------------------------------------------------------
// Metrics::Counter Implementation
//------------------------------------------------------------------------------

Metrics::Counter::Counter(const std::string& name,
                           const std::string& help,
                           const std::map<std::string, std::string>& labels)
    : Metric(name, help, Type::COUNTER, labels)
    , value(0.0)
{
}

void
Metrics::Counter::increment(double value)
{
    if (value < 0.0) {
        // Prometheus 计数器不能减少
        return;
    }
    double current = this->value.load();
    double desired = current + value;
    // 使用compare_exchange_weak直到成功更新值
    while (!this->value.compare_exchange_weak(current, desired)) {
        desired = current + value;
    }
}

double
Metrics::Counter::get() const
{
    return value.load();
}

std::string
Metrics::Counter::getValue() const
{
    std::ostringstream oss;
    
    // 构建标签字符串
    std::string labelStr;
    if (!labels.empty()) {
        std::ostringstream labelOss;
        labelOss << "{";
        bool first = true;
        for (const auto& label : labels) {
            if (!first) {
                labelOss << ",";
            }
            labelOss << label.first << "=\"" << label.second << "\"";
            first = false;
        }
        labelOss << "}";
        labelStr = labelOss.str();
    }
    
    oss << name << labelStr << " " << get() << "\n";
    return oss.str();
}

//------------------------------------------------------------------------------
// Metrics::Gauge Implementation
//------------------------------------------------------------------------------

Metrics::Gauge::Gauge(const std::string& name,
                       const std::string& help,
                       const std::map<std::string, std::string>& labels)
    : Metric(name, help, Type::GAUGE, labels)
    , value(0.0)
{
}

void
Metrics::Gauge::set(double value)
{
    this->value.store(value);
}

void
Metrics::Gauge::increment(double value)
{
    double current = this->value.load();
    double desired = current + value;
    // 使用compare_exchange_weak直到成功更新值
    while (!this->value.compare_exchange_weak(current, desired)) {
        desired = current + value;
    }
}

void
Metrics::Gauge::decrement(double value)
{
    double current = this->value.load();
    double desired = current - value;
    // 使用compare_exchange_weak直到成功更新值
    while (!this->value.compare_exchange_weak(current, desired)) {
        desired = current - value;
    }
}

double
Metrics::Gauge::get() const
{
    return value.load();
}

std::string
Metrics::Gauge::getValue() const
{
    std::ostringstream oss;
    
    // 构建标签字符串
    std::string labelStr;
    if (!labels.empty()) {
        std::ostringstream labelOss;
        labelOss << "{";
        bool first = true;
        for (const auto& label : labels) {
            if (!first) {
                labelOss << ",";
            }
            labelOss << label.first << "=\"" << label.second << "\"";
            first = false;
        }
        labelOss << "}";
        labelStr = labelOss.str();
    }
    
    oss << name << labelStr << " " << get() << "\n";
    return oss.str();
}

//------------------------------------------------------------------------------
// Metrics::Histogram Implementation
//------------------------------------------------------------------------------

Metrics::Histogram::Histogram(const std::string& name,
                               const std::string& help,
                               const std::vector<double>& buckets,
                               const std::map<std::string, std::string>& labels)
    : Metric(name, help, Type::HISTOGRAM, labels)
    , buckets(buckets)
    , bucketCounts(buckets.size() + 1)  // 包括 +Inf 桶
    , sum(0.0)
    , count(0)
    , mutex()
{
    // 确保桶是有序的
    std::sort(this->buckets.begin(), this->buckets.end());
    
    // 初始化所有桶的计数为 0
    for (auto& count : bucketCounts) {
        count.store(0);
    }
}

void
Metrics::Histogram::observe(double value)
{
    std::lock_guard<Core::Mutex> lock(mutex);
    
    // 更新总计数和总和
    ++count;
    double current = sum.load();
    double desired = current + value;
    while (!sum.compare_exchange_weak(current, desired)) {
        desired = current + value;
    }
    
    // 更新适当的桶计数
    for (size_t i = 0; i < buckets.size(); ++i) {
        if (value <= buckets[i]) {
            ++bucketCounts[i];
        }
    }
    
    // +Inf 桶总是包含所有观察值
    ++bucketCounts[bucketCounts.size() - 1];
}

std::string
Metrics::Histogram::getValue() const
{
    std::lock_guard<Core::Mutex> lock(mutex);
    
    std::ostringstream oss;
    
    // 基础标签字符串
    std::string baseLabelStr;
    if (!labels.empty()) {
        std::ostringstream labelOss;
        bool first = true;
        for (const auto& label : labels) {
            if (!first) {
                labelOss << ",";
            }
            labelOss << label.first << "=\"" << label.second << "\"";
            first = false;
        }
        baseLabelStr = labelOss.str();
    }
    
    // 输出每个桶
    for (size_t i = 0; i < buckets.size(); ++i) {
        double threshold = buckets[i];
        uint64_t bucketCount = bucketCounts[i].load();
        
        std::string labelStr;
        if (baseLabelStr.empty()) {
            labelStr = Core::StringUtil::format("{le=\"%.6g\"}", threshold);
        } else {
            labelStr = Core::StringUtil::format("{%s,le=\"%.6g\"}", baseLabelStr.c_str(), threshold);
        }
        
        oss << name << "_bucket" << labelStr << " " << bucketCount << "\n";
    }
    
    // +Inf 桶
    std::string labelStr;
    if (baseLabelStr.empty()) {
        labelStr = "{le=\"+Inf\"}";
    } else {
        labelStr = Core::StringUtil::format("{%s,le=\"+Inf\"}", baseLabelStr.c_str());
    }
    
    oss << name << "_bucket" << labelStr << " " << count.load() << "\n";
    
    // 输出总和和计数
    if (baseLabelStr.empty()) {
        oss << name << "_sum " << sum.load() << "\n";
        oss << name << "_count " << count.load() << "\n";
    } else {
        oss << name << "_sum{" << baseLabelStr << "} " << sum.load() << "\n";
        oss << name << "_count{" << baseLabelStr << "} " << count.load() << "\n";
    }
    
    return oss.str();
}

//------------------------------------------------------------------------------
// Metrics Implementation
//------------------------------------------------------------------------------

Metrics&
Metrics::getInstance()
{
    static Metrics instance;
    return instance;
}

Metrics::Metrics()
    : mutex()
    , metrics()
{
}

std::shared_ptr<Metrics::Counter>
Metrics::createCounter(const std::string& name,
                        const std::string& help,
                        const std::map<std::string, std::string>& labels)
{
    return getOrCreateMetric<Counter>(name, labels, name, help, labels);
}

std::shared_ptr<Metrics::Gauge>
Metrics::createGauge(const std::string& name,
                      const std::string& help,
                      const std::map<std::string, std::string>& labels)
{
    return getOrCreateMetric<Gauge>(name, labels, name, help, labels);
}

std::shared_ptr<Metrics::Histogram>
Metrics::createHistogram(const std::string& name,
                          const std::string& help,
                          const std::vector<double>& buckets,
                          const std::map<std::string, std::string>& labels)
{
    return getOrCreateMetric<Histogram>(name, labels, name, help, buckets, labels);
}

std::string
Metrics::getMetricsOutput() const
{
    std::lock_guard<Core::Mutex> lock(mutex);
    std::ostringstream oss;
    
    for (const auto& metric : metrics) {
        oss << metric->getOutput();
    }
    
    return oss.str();
}

template<typename T, typename... Args>
std::shared_ptr<T>
Metrics::getOrCreateMetric(const std::string& name,
                           const std::map<std::string, std::string>& labels,
                           Args&&... args)
{
    std::lock_guard<Core::Mutex> lock(mutex);
    
    // 检查是否已经存在具有相同名称和标签的指标
    for (const auto& metric : metrics) {
        auto typedMetric = std::dynamic_pointer_cast<T>(metric);
        if (typedMetric && metric->getName() == name) {
            // 比较标签
            bool labelsMatch = true;
            const auto& metricLabels = metric->getLabels();
            if (labels.size() == metricLabels.size()) {
                for (const auto& labelPair : labels) {
                    auto it = metricLabels.find(labelPair.first);
                    if (it == metricLabels.end() || it->second != labelPair.second) {
                        labelsMatch = false;
                        break;
                    }
                }
                if (labelsMatch) {
                    return typedMetric;
                }
            }
        }
    }
    
    // 不存在，创建一个新的
    auto metric = std::make_shared<T>(std::forward<Args>(args)...);
    metrics.push_back(metric);
    return metric;
}

} // namespace Server
} // namespace LogCabin
