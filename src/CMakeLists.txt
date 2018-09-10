cmake_minimum_required(VERSION 2.8)

project(vadec)

set (CMAKE_CXX_STANDARD 11)

include_directories (vadec /usr/local/include)
# link_directories must be put before add_executable
link_directories(/usr/local/lib/)

add_executable(vadec vadec.cpp)

target_link_libraries(vadec avutil avformat avcodec avfilter avdevice)

