# Storage库源代码
# 处理proto文件
set(STORAGE_PROTO_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/SegmentedLog.proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/SimpleFileLog.proto"
)

# 修改生成选项，添加 Protocol 目录作为 import 路径
set(PROTO_IMPORT_DIRS
    "${CMAKE_SOURCE_DIR}/Protocol"
    "${CMAKE_BINARY_DIR}/Protocol"
)

# 手动创建 protobuf 生成命令，以便添加 import 路径
set(STORAGE_PROTO_SRCS "")
set(STORAGE_PROTO_HDRS "")
foreach(proto ${STORAGE_PROTO_FILES})
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
    list(APPEND STORAGE_PROTO_SRCS ${PROTO_SRC})
    list(APPEND STORAGE_PROTO_HDRS ${PROTO_HDR})
endforeach()

# 确保生成的文件被视为源文件
set_source_files_properties(${STORAGE_PROTO_SRCS} ${STORAGE_PROTO_HDRS} PROPERTIES GENERATED TRUE)

# 添加必要的源文件
set(STORAGE_SOURCES
    SegmentedLog.cc
    SimpleFileLog.cc
    SnapshotFile.cc
    Tool.cc
    LogFactory.cc
    MemoryLog.cc
    LSMStorage.cc
    ${STORAGE_PROTO_SRCS}
)

# 如果定义了LSM存储，添加相关源文件
if(LOGCABIN_ENABLE_LSM_STORAGE)
    list(APPEND STORAGE_SOURCES
        LSMStorage.cc
        LSMStorage.h
    )
endif()

# 创建Storage库
add_library(Storage STATIC 
    ${STORAGE_SOURCES}
    ${STORAGE_PROTO_HDRS} # Add generated headers to the library sources
)

# 包含生成的头文件目录
target_include_directories(Storage PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/Protocol
    ${CMAKE_BINARY_DIR}/Server  # 添加Server目录以访问SnapshotMetadata.pb.h等文件
    ${CMAKE_SOURCE_DIR} # 添加源码根目录，以便可以通过 "Server/SnapshotMetadata.pb.h" 包含
)

target_link_libraries(Storage
    Core
    Protocol
    ${PROTOBUF_LIBRARIES}
    ${CRYPTOPP_LIBRARIES}
    zstd
)

# 存储工具可执行文件
add_executable(StorageTool Tool.cc)
target_include_directories(StorageTool PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/Server
    ${CMAKE_SOURCE_DIR} # 添加源码根目录，以便可以通过 "Server/SnapshotMetadata.pb.h" 包含
)
target_link_libraries(StorageTool 
    Storage 
    Core
    Protocol # Added Protocol library
    ${PROTOBUF_LIBRARIES}
    ${CRYPTOPP_LIBRARIES}
    zstd
)
set_target_properties(StorageTool PROPERTIES OUTPUT_NAME "Tool")

# 测试
if(LOGCABIN_BUILD_TESTS)
    set(STORAGE_TEST_SOURCES
        LogFactoryTest.cc
        SegmentedLogTest.cc
        SimpleFileLogTest.cc
        SnapshotFileTest.cc
    )
    
    foreach(test_file ${STORAGE_TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(${test_name} ${test_file})
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_BINARY_DIR}/Server
            ${CMAKE_SOURCE_DIR} # 添加源码根目录，以便可以通过 "Server/SnapshotMetadata.pb.h" 包含
        )
        target_link_libraries(${test_name} Storage Core Protocol gtest gtest_main pthread)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
