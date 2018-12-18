cmake_minimum_required(VERSION 2.8)
project(sample-c)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -rdynamic")

include_directories(../../include)

add_executable(sample-c ./sample.c ../../src/memory-trace.c)
target_link_libraries(sample-c pthread)
