cmake_minimum_required(VERSION 2.6)
project(pasched)
set(CMAKE_MODULE_PATH ${pasched_SOURCE_DIR}/cmake/Modules ${CMAKE_MODULE_PATH})

include(FindGLPK)
if(NOT GLPK_FOUND)
    message(SEND_ERROR "You need GLPK to compile this library")
endif(NOT GLPK_FOUND)

file(GLOB pasched_SOURCE_FILES src/*.cpp)
file(GLOB pasched_PUBLIC_HEADERS include/*.hpp)
file(GLOB pasched_LIBPASCHED_HEADERS include/libpasched/*.hpp)

add_definitions(-g -Wall)
include_directories(${pasched_SOURCE_DIR}/include/libpasched)
include_directories(${GLPK_INCLUDE_DIR})
add_library(pasched SHARED ${pasched_SOURCE_FILES})
target_link_libraries(pasched ${GLPK_LIBRARY})

install(TARGETS pasched LIBRARY DESTINATION lib)
install(FILES ${pasched_PUBLIC_HEADERS} DESTINATION include)
install(FILES ${pasched_LIBPASCHED_HEADERS} DESTINATION include/libpasched)
