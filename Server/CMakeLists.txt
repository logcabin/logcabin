# Proto文件
set(SERVER_PROTO_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/SnapshotMetadata.proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/SnapshotStateMachine.proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/SnapshotStats.proto"
)

# 手动创建 protobuf 生成命令，以便添加 import 路径
set(SERVER_PROTO_SRCS "")
set(SERVER_PROTO_HDRS "")
foreach(proto ${SERVER_PROTO_FILES})
    get_filename_component(BASENAME ${proto} NAME_WE)
    set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}.pb.cc")
    set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}.pb.h")
    
    add_custom_command(
        OUTPUT ${PROTO_SRC} ${PROTO_HDR}
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
             -I${CMAKE_CURRENT_SOURCE_DIR}
             -I${CMAKE_SOURCE_DIR}/Protocol
             -I${CMAKE_BINARY_DIR}/Protocol
             ${proto}
        DEPENDS ${proto}
        COMMENT "Running C++ protocol buffer compiler on ${proto}"
        VERBATIM
    )
    list(APPEND SERVER_PROTO_SRCS ${PROTO_SRC})
    list(APPEND SERVER_PROTO_HDRS ${PROTO_HDR})
endforeach()

# 确保生成的文件被视为源文件
set_source_files_properties(${SERVER_PROTO_SRCS} ${SERVER_PROTO_HDRS} PROPERTIES GENERATED TRUE)

# Server库源代码
set(SERVER_SOURCES
    ClientService.cc
    Globals.cc
    Main.cc
    RaftConsensus.cc
    RaftService.cc
    ServerStats.cc
    StateMachine.cc
    Metrics.cc
    MetricsServer.cc
    ${SERVER_PROTO_SRCS}
)

# Web管理界面源文件（如果启用）
if(LOGCABIN_ENABLE_WEBADMIN)
    set(SERVER_SOURCES ${SERVER_SOURCES}
    )
endif()

# 创建Server库
add_library(Server STATIC 
    ${SERVER_SOURCES}
)

# 包含目录
target_include_directories(Server PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
)

# 编译选项
if(LOGCABIN_ENABLE_METRICS)
    target_compile_definitions(Server PRIVATE -DENABLE_METRICS)
endif()

if(LOGCABIN_ENABLE_TELEMETRY)
    target_compile_definitions(Server PRIVATE -DENABLE_TELEMETRY)
endif()

target_link_libraries(Server
    Storage
    Tree
    Client
    Protocol
    RPC
    Event
    Core
    ${PROTOBUF_LIBRARIES}
    ${CRYPTOPP_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

# 主可执行文件 (注释掉，因为在根目录中已经定义)
# add_executable(LogCabin Main.cc)
# target_link_libraries(LogCabin ...)

# 测试
if(LOGCABIN_BUILD_TESTS)
    set(SERVER_TEST_SOURCES
        ClientServiceTest.cc
        RaftConsensusTest.cc
        ServerStatsTest.cc
        StateMachineTest.cc
    )
    
    # Web管理界面测试
    if(LOGCABIN_ENABLE_WEBADMIN)
        list(APPEND SERVER_TEST_SOURCES
        )
    endif()
    
    foreach(test_file ${SERVER_TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(${test_name} ${test_file})
        target_link_libraries(${test_name} Server Storage Tree Client Protocol RPC Event Core gtest gtest_main)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
