cmake_minimum_required(VERSION 3.16) # version can be different

project(my_cpp_project) #name of your project

add_subdirectory(googletest) # add googletest subdirectory

include_directories(googletest/include) # this is so we can #include <gtest/gtest.h>

add_executable(test UnitTests.cpp) # add this executable

target_link_libraries(test PRIVATE gtest) # link google test to this executable