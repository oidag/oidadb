cmake_minimum_required(VERSION 3.21)

project(edb C)

set(CMAKE_C_STANDARD 17)

file(GLOB sources c.src/*.c)
include_directories(c.src/include)
add_library(libedb ${sources})

# tests...
add_executable(test_pages c.src/tests/page_main.c c.src/edbp.c c.src/edbd.c c.src/errors.c)
TARGET_LINK_LIBRARIES(test_pages pthread)
add_executable(test_host c.src/tests/host_main.c ${sources})
TARGET_LINK_LIBRARIES(test_host pthread rt)


file(GLOB thetool_src c.src/tests/stupididea/*.c)
add_executable(stupididea ${thetool_src})
target_link_libraries(stupididea GL glfw drawtext freetype)
