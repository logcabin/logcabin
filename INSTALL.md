# 安装 LogCabin

## 系统要求

- GCC 4.7+ 或 Clang 3.4+（推荐 GCC 7+ 或 Clang 5+ 以获得 C++17 支持）
- SCons 2.0+
- Protocol Buffers 2.6.0+ 开发库
- Crypto++ 5.6.0+ 开发库
- OpenSSL 开发库

## 从源码安装

### 安装依赖

Ubuntu/Debian:
```bash
sudo apt-get install build-essential g++ python3-dev scons libprotobuf-dev protobuf-compiler libcrypto++-dev libssl-dev
```

CentOS/RHEL:
```bash
sudo yum install gcc-c++ python3-devel scons protobuf-devel protobuf-compiler cryptopp-devel openssl-devel
```

### 编译安装

```bash
# 克隆项目
git clone https://github.com/logcabin/logcabin.git
cd logcabin

# 默认构建（调试模式）
scons

# 发布模式构建
scons BUILDTYPE=RELEASE

# 运行测试
scons test

# 安装（默认安装到 /usr 目录）
sudo scons install

# 自定义安装路径
sudo scons install PREFIX=/usr/local SYSCONFDIR=/etc LOCALSTATEDIR=/var
```

## Docker 安装与使用

### 构建 Docker 镜像
```bash
docker build -t logcabin .
```

### 运行单节点 LogCabin
```bash
docker run -d --name logcabin -p 5254:5254 \
  -v /path/to/config:/etc/logcabin \
  -v logcabin_data:/var/lib/logcabin \
  -v logcabin_logs:/var/log/logcabin \
  logcabin
```

### 使用 Docker Compose 运行集群
```bash
docker-compose up -d
```

## 使用 RPM 包安装

生成 RPM 包:
```bash
scons rpm
```

安装 RPM 包:
```bash
sudo rpm -i build/logcabin-*.rpm
```

## 配置

安装后，编辑配置文件，位于 `/etc/logcabin/logcabin.conf`（或自定义路径）：

```
# 示例配置内容
serverId = 1
listenAddresses = 192.168.1.10:5254
storagePath = /var/lib/logcabin
bootstrap = yes
```
