cmake_minimum_required(VERSION 2.6)
project(ddd)

set(CMAKE_CXX_FLAGS "-Wall -pthread -std=c++11")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "BUILD_TYPE is ${CMAKE_BUILD_TYPE}")

include_directories(include)

set(INCLUDES
  include/Basic.hpp
  include/DaTrie.hpp
  include/Dictionary.hpp
  include/DictionaryMLT.hpp
  include/DictionarySGL.hpp
  )
add_executable(Benchmark Benchmark.cpp ${INCLUDES})

enable_testing()
add_executable(Test Test.cpp)
add_test(NAME Test COMMAND $<TARGET_FILE:Test>)
