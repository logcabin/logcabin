# 多阶段构建以减小镜像大小
FROM ubuntu:22.04 AS builder

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    python3-dev \
    scons \
    libprotobuf-dev \
    protobuf-compiler \
    libcrypto++-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /build

# 复制源代码
COPY . .

# 构建项目（使用优化过的Release模式）
RUN scons BUILDTYPE=RELEASE

# 第二阶段：运行时镜像
FROM ubuntu:22.04

# 安装运行时依赖
RUN apt-get update && apt-get install -y \
    libprotobuf23 \
    libcrypto++6 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

# 创建非root用户和所需目录
RUN useradd -r -s /bin/false logcabin && \
    mkdir -p /var/log/logcabin /var/lib/logcabin /etc/logcabin && \
    chown -R logcabin:logcabin /var/log/logcabin /var/lib/logcabin /etc/logcabin

# 从构建阶段复制构建的二进制文件和配置
COPY --from=builder /build/build/LogCabin /usr/bin/LogCabin
COPY --from=builder /build/sample.conf /etc/logcabin/logcabin.conf
COPY --from=builder /build/config/prometheus.conf /etc/logcabin/prometheus.conf

# 设置默认配置
RUN echo "metricsEnabled = true" >> /etc/logcabin/logcabin.conf && \
    echo "metricsListenAddress = 0.0.0.0" >> /etc/logcabin/logcabin.conf && \
    echo "metricsPort = 9090" >> /etc/logcabin/logcabin.conf

# 暴露端口
EXPOSE 5254 9090

# 设置数据卷
VOLUME ["/var/log/logcabin", "/var/lib/logcabin", "/etc/logcabin"]

# 切换到非root用户
USER logcabin

# 健康检查
HEALTHCHECK --interval=30s --timeout=10s --retries=3 \
  CMD curl -f http://localhost:9090/metrics || exit 1

# 设置入口点
ENTRYPOINT ["/usr/bin/LogCabin", "--config", "/etc/logcabin/logcabin.conf"]
