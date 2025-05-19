# 定义 Core 库
set(CORE_SOURCES
    Buffer.cc
    Checksum.cc
    ConditionVariable.cc
    Config.cc
    Debug.cc
    ProtoBuf.cc
    Random.cc
    RollingStat.cc
    StringUtil.cc
    ThreadId.cc
    Time.cc
    Util.cc
)

# Core has its own test protobuf file.
protobuf_generate_cpp(CORE_PROTO_SRCS CORE_PROTO_HDRS ProtoBufTest.proto)

# 创建 Core 库
add_library(Core STATIC ${CORE_SOURCES} ${CORE_PROTO_SRCS})

# Core needs to find its own generated headers and headers from Protocol and Server.
target_include_directories(Core PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}"  # For Core's own generated ProtoBufTest.pb.h
    # Protocol will export its include directory for ServerStats.pb.h etc.
    "${CMAKE_BINARY_DIR}/Server" # For SnapshotMetadata.pb.h from Server module
)

target_link_libraries(Core PUBLIC # Changed to PUBLIC if other modules depend on Core's specifics
    Protocol # Core depends on Protocol for ServerStats.pb.h
    ${PROTOBUF_LIBRARIES}
    ${CRYPTOPP_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

# 测试
if(LOGCABIN_BUILD_TESTS)
    set(CORE_TEST_SOURCES
        BufferTest.cc
        ChecksumTest.cc
        CompatHashTest.cc
        ConditionVariableTest.cc
        ConfigTest.cc
        DebugTest.cc
        ProtoBufTest.cc
        RandomTest.cc
        RollingStatTest.cc
        STLUtilTest.cc
        StringUtilTest.cc
        ThreadIdTest.cc
        TimeTest.cc
        UtilTest.cc
    )
    
    foreach(test_file ${CORE_TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(${test_name} ${test_file})
        
        # 添加与服务器相关的头文件目录
        target_include_directories(${test_name} PRIVATE 
            "${CMAKE_CURRENT_BINARY_DIR}" 
            "${CMAKE_BINARY_DIR}/Server"
            "${CMAKE_SOURCE_DIR}/gtest/include" # 添加gtest头文件路径
        )
        
        # 正确链接所需的库，特别是gtest库
        if (${test_name} STREQUAL "DebugTest")
            target_link_libraries(${test_name} Core Storage Protocol RPC Event gtest gtest_main pthread)
        else()
            target_link_libraries(${test_name} Core Protocol gtest gtest_main pthread ${PROTOBUF_LIBRARIES})
        endif()
        
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
