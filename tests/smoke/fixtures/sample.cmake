# A CMake module.
cmake_minimum_required(VERSION 3.24)

set(MY_SOURCES main.c util.c)
option(ENABLE_THING "Turn the thing on" ON)

if(APPLE AND NOT WIN32)
    message(STATUS "on apple: ${MY_SOURCES}")
endif()

add_library(demo STATIC ${MY_SOURCES})
target_link_libraries(demo PUBLIC other)
