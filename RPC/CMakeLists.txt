# 定义 RPC 库
set(RPC_SOURCES
    Address.cc
    ClientRPC.cc
    ClientSession.cc
    MessageSocket.cc
    OpaqueClientRPC.cc
    OpaqueServer.cc
    OpaqueServerRPC.cc
    Protocol.cc
    Server.cc
    ServerRPC.cc
    ThreadDispatchService.cc
    TLS.cc
)

# 创建RPC库
add_library(RPC STATIC ${RPC_SOURCES})
target_link_libraries(RPC
    Protocol
    Event
    Core
    ${PROTOBUF_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

# 测试
if(LOGCABIN_BUILD_TESTS)
    set(RPC_TEST_SOURCES
        AddressTest.cc
        ClientRPCTest.cc
        ClientServerTest.cc
        ClientSessionTest.cc
        MessageSocketTest.cc
        OpaqueClientRPCTest.cc
        OpaqueServerRPCTest.cc
        OpaqueServerTest.cc
        ServerRPCTest.cc
        ServerTest.cc
        ThreadDispatchServiceTest.cc
        ServiceMock.cc
        # TLSTest.cc (不存在)
    )
    
    foreach(test_file ${RPC_TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(${test_name} ${test_file})
        target_link_libraries(${test_name} RPC Protocol Event Core gtest gtest_main)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
