cmake_minimum_required(VERSION 2.8)

project(vadec)

set (SOURCES_ 
    test.cpp 
    accel.cpp 
    accel.hpp 
    frame.cpp 
    frame.hpp 
)

set (CMAKE_CXX_STANDARD 11)

include_directories (vadec /usr/local/include)
# link_directories must be put before add_executable
link_directories(/usr/local/lib/)

set (FFMPEG_LIBS avutil avformat avcodec avfilter avdevice)

add_executable(test ${SOURCES_})
target_link_libraries(test ${FFMPEG_LIBS})
