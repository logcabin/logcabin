#ifndef LOGCABIN_PROTOBUF_LOGCONFIG_H
#define LOGCABIN_PROTOBUF_LOGCONFIG_H

#include <google/protobuf/message.h>

namespace LogCabin {
namespace ProtoBuf {

/**
 * 代表Log配置的简单类。
 * 这是一个简单的包装类，可以在实际项目中扩展。
 */
class LogConfig : public google::protobuf::Message {
public:
    // 必须的构造函数
    LogConfig() {}
    LogConfig(const LogConfig& other) : google::protobuf::Message() {}
    
    // 必须的虚函数实现
    virtual ~LogConfig() {}
    
    // 实现Message接口
    LogConfig* New() const override { return new LogConfig(); }
    void Clear() override {}
    bool IsInitialized() const override { return true; }
    void MergeFrom(const google::protobuf::Message& from) override {}
    void CopyFrom(const google::protobuf::Message& from) override {}
    void MergeFrom(const LogConfig& from) {}
    void CopyFrom(const LogConfig& from) {}
    void Swap(LogConfig* other) {}
    
    // 反序列化函数
    bool ParseFromString(const std::string& data) override { return true; }
    bool ParseFromArray(const void* data, int size) override { return true; }
    
    // 序列化函数
    void SerializeWithCachedSizes(google::protobuf::io::CodedOutputStream* output) const override {}
    google::protobuf::uint8* SerializeWithCachedSizesToArray(google::protobuf::uint8* output) const override { return output; }
    
    // 获取大小的函数
    int ByteSize() const override { return 0; }
    int GetCachedSize() const { return 0; }
    
    // 反射接口
    const google::protobuf::Descriptor* GetDescriptor() const override { return nullptr; }
    const google::protobuf::Reflection* GetReflection() const override { return nullptr; }
    const google::protobuf::UnknownFieldSet& unknown_fields() const { static google::protobuf::UnknownFieldSet unknownFields; return unknownFields; }
    google::protobuf::UnknownFieldSet* mutable_unknown_fields() { static google::protobuf::UnknownFieldSet unknownFields; return &unknownFields; }
    
    // LSM特定配置
    bool has_lsm_buffer_size_mb() const { return false; }
    uint64_t lsm_buffer_size_mb() const { return 64; }
    
    bool has_lsm_compression_enabled() const { return false; }
    bool lsm_compression_enabled() const { return true; }
    
    bool has_lsm_compression_algorithm() const { return false; }
    std::string lsm_compression_algorithm() const { return "zstd"; }
    
    bool has_lsm_max_levels() const { return false; }
    uint32_t lsm_max_levels() const { return 7; }
    
    bool has_lsm_bloom_filter_fp_rate() const { return false; }
    double lsm_bloom_filter_fp_rate() const { return 0.01; }
};

} // namespace ProtoBuf
} // namespace LogCabin

#endif // LOGCABIN_PROTOBUF_LOGCONFIG_H
