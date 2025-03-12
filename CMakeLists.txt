cmake_minimum_required(VERSION 3.8)
project(abcscript CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O3)

add_subdirectory(abc)
add_subdirectory(argparse)

add_executable(opt main.cpp)
target_link_libraries(opt libabc argparse)

add_executable(opt2 main2.cpp)
target_link_libraries(opt2 libabc argparse)

add_executable(opt3 main3.cpp)
target_link_libraries(opt3 libabc argparse)

add_executable(opt4 main4.cpp)
target_link_libraries(opt4 libabc argparse)
