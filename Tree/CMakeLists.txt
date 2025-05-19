# Tree库源代码
set(TREE_SOURCES
    Tree.cc
    ProtoBuf.cc
)

# 生成协议文件
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS Snapshot.proto)

# 创建Tree库
add_library(Tree STATIC 
    ${TREE_SOURCES}
    ${PROTO_SRCS}
)

target_link_libraries(Tree
    Protocol
    Core
    ${PROTOBUF_LIBRARIES}
)

# 测试
if(LOGCABIN_BUILD_TESTS)
    set(TREE_TEST_SOURCES
        TreeTest.cc
        ProtoBufTest.cc
    )
    
    foreach(test_file ${TREE_TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        if(test_name STREQUAL "ProtoBufTest")
            set(test_name TreeProtoBufTest)
        endif()
        add_executable(${test_name} ${test_file})
        target_link_libraries(${test_name} Tree Protocol Core gtest gtest_main)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
