cmake_minimum_required(VERSION 2.6)
project(pasched_tools)
set(CMAKE_MODULE_PATH ${pasched_tools_SOURCE_DIR}/../cmake/Modules ${CMAKE_MODULE_PATH})

include(FindPASCHED)
if(NOT PASCHED_FOUND)
    message(SEND_ERROR "You need libpasched to compile this program")
endif(NOT PASCHED_FOUND)

include_directories(${PASCHED_INCLUDE_DIR})

if(CMAKE_COMPILER_IS_GNUCC)
    add_definitions("-Wall -pedantic")
endif(CMAKE_COMPILER_IS_GNUCC)

add_executable(cplex2sol cplex2sol.cpp)
target_link_libraries(cplex2sol ${PASCHED_LIBRARY})
