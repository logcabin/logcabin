# 定义 Event 库
set(EVENT_SOURCES
    File.cc
    Loop.cc
    Signal.cc
    Timer.cc
)

# 创建 Event 库
add_library(Event STATIC ${EVENT_SOURCES})
target_link_libraries(Event Core)

# 测试
if(LOGCABIN_BUILD_TESTS)
    set(EVENT_TEST_SOURCES
        FileTest.cc
        LoopTest.cc
        SignalTest.cc
        TimerTest.cc
    )
    
    foreach(test_file ${EVENT_TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(${test_name} ${test_file})
        target_link_libraries(${test_name} Event Core gtest gtest_main)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
