cmake_minimum_required(VERSION 2.8)
project(memory_trace C CXX)

add_library(memory_trace SHARED ./src/memory-trace.c)
set_target_properties(memory_trace PROPERTIES
  LINK_FLAGS "-fpic -g -rdynamic"
  COMPILE_FLAGS ""
)

add_executable(sample-c ./samples/sample.c)
set_target_properties(memory_trace PROPERTIES
  LINK_FLAGS "-g"
  COMPILE_FLAGS ""
)

add_executable(sample-cc ./samples/sample.cc)
set_target_properties(memory_trace PROPERTIES
  LINK_FLAGS "-g"
  COMPILE_FLAGS ""
)

target_link_libraries(sample-c pthread memory_trace)
target_link_libraries(sample-cc pthread memory_trace)
