cmake_minimum_required(VERSION 3.8)
project(abcscript CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-g -O3) # -DNDEBUG

add_subdirectory(abc)
add_subdirectory(argparse)

add_executable(opt main.cpp)
target_link_libraries(opt libabc argparse)
