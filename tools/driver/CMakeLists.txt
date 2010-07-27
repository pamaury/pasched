cmake_minimum_required(VERSION 2.6)
project(pasched_tools)
set(CMAKE_MODULE_PATH ${pasched_tools_SOURCE_DIR}/../cmake/Modules ${CMAKE_MODULE_PATH})

include(FindPASCHED)
if(NOT PASCHED_FOUND)
    message(SEND_ERROR "You need libpasched to compile this program")
endif(NOT PASCHED_FOUND)

include_directories(${PASCHED_INCLUDE_DIR})

set(CMAKE_BUILD_TYPE Profile)
set(CMAKE_CXX_FLAGS_PROFILE "-pg -g")
set(CMAKE_C_FLAGS_PROFILE "-pg -g")

if(CMAKE_COMPILER_IS_GNUCC)
    add_definitions("-Wall -pedantic")
endif(CMAKE_COMPILER_IS_GNUCC)

add_executable(driver driver.cpp)
target_link_libraries(driver ${PASCHED_LIBRARY})
