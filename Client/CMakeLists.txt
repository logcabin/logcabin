# Client库源代码
set(CLIENT_SOURCES
    Backoff.cc
    Client.cc
    ClientImpl.cc
    LeaderRPC.cc
    MockClientImpl.cc
    ServerControl.cc
    SessionManager.cc
    Util.cc
)

# 创建Client库
add_library(Client STATIC ${CLIENT_SOURCES})
target_link_libraries(Client
    Tree
    Protocol
    RPC
    Event
    Core
    ${PROTOBUF_LIBRARIES}
)

# ServerControl客户端工具
add_executable(ServerControl ServerControl.cc)
target_link_libraries(ServerControl
    Client 
    Protocol
    Core
    ${PROTOBUF_LIBRARIES}
)

# 测试
if(LOGCABIN_BUILD_TESTS)
    set(CLIENT_TEST_SOURCES
        BackoffTest.cc
        ClientImplTest.cc
        ClientTest.cc
        LeaderRPCTest.cc
        MockClientImplTest.cc
        SessionManagerTest.cc
        UtilTest.cc
    )
    
    foreach(test_file ${CLIENT_TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        if(test_name STREQUAL "UtilTest")
            set(test_name ClientUtilTest)
        endif()
        add_executable(${test_name} ${test_file})
        target_link_libraries(${test_name} Client Protocol RPC Event Core gtest gtest_main)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
