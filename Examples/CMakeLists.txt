# 示例程序源代码
set(EXAMPLE_SOURCES
    Benchmark.cc
    FailoverTest.cc
    HelloWorld.cc
    Reconfigure.cc
    SmokeTest.cc
    TreeOps.cc
)

# 创建示例程序可执行文件
add_executable(Benchmark Benchmark.cc)
add_executable(FailoverTest FailoverTest.cc)
add_executable(HelloWorld HelloWorld.cc)
add_executable(Reconfigure Reconfigure.cc)
add_executable(SmokeTest SmokeTest.cc)
add_executable(TreeOps TreeOps.cc)

# 为每个示例程序设置链接库
foreach(target Benchmark FailoverTest HelloWorld Reconfigure SmokeTest TreeOps)
    target_link_libraries(${target}
        Client
        Tree
        Protocol
        RPC
        Event
        Core
        ${PROTOBUF_LIBRARIES}
        ${CRYPTOPP_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        rt
    )
endforeach()

# 测试
if(LOGCABIN_BUILD_TESTS)
    add_executable(ReconfigureTest ReconfigureTest.cc)
    target_link_libraries(ReconfigureTest
        Client
        Tree
        Protocol
        RPC
        Event
        Core
        gtest
        gtest_main
        ${PROTOBUF_LIBRARIES}
        ${CRYPTOPP_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        rt
    )
    add_test(NAME ReconfigureTest COMMAND ReconfigureTest)
endif()
