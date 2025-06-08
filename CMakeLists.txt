cmake_minimum_required(VERSION 3.18)	

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

set(CMAKE_BUILD_TYPE DEBUG)

project(myDriverFTDI)

# include_directories(C:/Windows/System32)
include_directories(ftSrc)

add_executable(main main.c)

set_target_properties(main PROPERTIES RUNTIME_OUTPUT_DIRECTORY ..)
# add_subdirectory(ftSrc)
link_directories(ftSrc)
target_link_libraries(main "${CMAKE_CURRENT_SOURCE_DIR}/ftSrc/ftd2xx64.dll")

