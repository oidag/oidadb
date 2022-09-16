cmake_minimum_required(VERSION 3.21)

project(fuck C)

set(CMAKE_C_STANDARD 17)

file(GLOB sources *.c)
include_directories(include)

add_library(edb ${sources})

add_executable(test_pages tests/page_main.c pages.c file-io.c errors.c)
TARGET_LINK_LIBRARIES(test_pages pthread)
add_executable(test_host tests/host_main.c ${sources})
TARGET_LINK_LIBRARIES(test_host pthread rt)

