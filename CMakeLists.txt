cmake_minimum_required(VERSION 3.21)

project(fuck C)

set(CMAKE_C_STANDARD 17)

file(GLOB sources *.c)
include_directories(include)

add_library(edb ${sources})