cmake_minimum_required(VERSION 3.13)

# 项目定义
project(LogCabin 
    VERSION 1.2.0
    DESCRIPTION "分布式一致性存储系统" 
    LANGUAGES CXX C)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# 安装路径配置
include(GNUInstallDirs)
set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "安装前缀")
set(SYSCONFDIR "/etc" CACHE PATH "系统配置目录")
set(LOCALSTATEDIR "/var" CACHE PATH "本地状态目录")

# 项目选项
option(LOGCABIN_BUILD_TESTS "构建测试" ON)
option(LOGCABIN_INSTALL_EXAMPLES "安装示例程序" ON)
option(LOGCABIN_BUILD_DOCS "构建文档" OFF)
option(LOGCABIN_ENABLE_TLS "启用TLS支持" ON)
option(LOGCABIN_ENABLE_METRICS "启用Prometheus指标" ON)

# 查找依赖库
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_library(CRYPTOPP_LIBRARIES cryptopp REQUIRED)
find_package(OpenSSL REQUIRED)

# 编译选项
add_compile_options(-Wall -Wextra -Wformat=2 -fPIC)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -DDEBUG)
else()
    add_compile_options(-DNDEBUG -O3 -flto -march=native -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

# 添加Google Test
add_subdirectory(gtest)

# 设置包含路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${PROTOBUF_INCLUDE_DIRS})

# 添加子目录
add_subdirectory(Core)
add_subdirectory(Event)
add_subdirectory(RPC)
add_subdirectory(Protocol)
add_subdirectory(Server) # Ensure Server is processed before Storage if Storage depends on Server's generated files
add_subdirectory(Tree)
add_subdirectory(Client)
add_subdirectory(Storage)
add_subdirectory(Examples)

if(LOGCABIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# 构建主程序
add_executable(LogCabin Server/Main.cc)
target_link_libraries(LogCabin
    Server
    Storage
    Tree
    Client
    Protocol
    RPC
    Event
    Core
    ${CMAKE_THREAD_LIBS_INIT}
    ${PROTOBUF_LIBRARIES}
    ${CRYPTOPP_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    rt
)

# 安装目标
install(TARGETS LogCabin
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装客户端工具
install(PROGRAMS 
    ${CMAKE_BINARY_DIR}/Client/ServerControl
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME logcabinctl
)

# 安装示例工具（修复语法）
install(PROGRAMS
    ${CMAKE_BINARY_DIR}/Examples/TreeOps
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(PROGRAMS
    ${CMAKE_BINARY_DIR}/Examples/Benchmark
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME logcabin-benchmark
)
install(PROGRAMS
    ${CMAKE_BINARY_DIR}/Examples/Reconfigure
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME logcabin-reconfigure
)
install(PROGRAMS
    ${CMAKE_BINARY_DIR}/Examples/SmokeTest
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME logcabin-smoketest
)

# 安装存储工具
install(PROGRAMS
    ${CMAKE_BINARY_DIR}/Storage/Tool
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME logcabin-storage
)

# 创建日志目录
install(DIRECTORY 
    DESTINATION ${LOCALSTATEDIR}/log/logcabin
    DIRECTORY_PERMISSIONS 
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_WRITE GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

# 打包支持
include(CPack)
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "logcabin")
set(CPACK_PACKAGE_VENDOR "LogCabin")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LogCabin是一个提供少量高度复制的一致性存储的分布式系统")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "developer@logcabin.github.io")
set(CPACK_RPM_PACKAGE_LICENSE "ISC")
