cmake_minimum_required(VERSION 3.14)
project(cfddns C)

set(CMAKE_C_STANDARD 11)

add_executable(cfddns main.c)

target_link_libraries(cfddns curl)