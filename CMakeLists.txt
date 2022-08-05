cmake_minimum_required(VERSION 3.23)
project(CWebServer C)

set(CMAKE_C_STANDARD 23)

include_directories(include/)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.c")

add_executable(CWebServer ${sources})
