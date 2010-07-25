cmake_minimum_required(VERSION 2.6)
project(pasched_tools)
set(CMAKE_MODULE_PATH ${pasched_tools_SOURCE_DIR}/../cmake/Modules ${CMAKE_MODULE_PATH})

include(FindPASCHED)
if(NOT PASCHED_FOUND)
    message(SEND_ERROR "You need libpasched to compile this program")
endif(NOT PASCHED_FOUND)

add_definitions(-g -pedantic -W -Wall)
include_directories(${PASCHED_INCLUDE_DIR})

add_executable(split2lsd split2lsd.cpp)
target_link_libraries(split2lsd ${PASCHED_LIBRARY})
